#include "signalk_auth.hpp"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "sk_auth";

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static volatile signalk_auth_state_t s_state = SK_AUTH_NONE;
static char s_base_url[128]   = {};   // "http://host:port"
static char s_client_id[40]   = {};   // UUID string
static char s_token[512]      = {};   // JWT token
static char s_error[128]      = {};   // Human-readable error
static TaskHandle_t s_task    = NULL;

static const char *NVS_NAMESPACE = "sk_auth";
static const char *NVS_KEY_ID   = "client_id";
static const char *NVS_KEY_TOK  = "token";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Convert "ws://host:port/path..." to "http://host:port"
static void derive_http_base(const char *ws_uri, char *out, size_t len)
{
    const char *src = ws_uri;
    const char *scheme = "http://";

    if (strncmp(src, "wss://", 6) == 0) {
        scheme = "https://";
        src += 6;
    } else if (strncmp(src, "ws://", 5) == 0) {
        scheme = "http://";
        src += 5;
    } else {
        // Not a WS URI — copy as-is, strip trailing path
        strncpy(out, ws_uri, len - 1);
        out[len - 1] = '\0';
        return;
    }

    // Find end of host:port (first '/' after scheme)
    const char *slash = strchr(src, '/');
    size_t host_len = slash ? (size_t)(slash - src) : strlen(src);

    snprintf(out, len, "%s%.*s", scheme, (int)host_len, src);
}

// Generate a random UUID v4 string and store in NVS
static void generate_uuid(char *out, size_t len)
{
    uint8_t bytes[16];
    for (int i = 0; i < 4; i++) {
        uint32_t r = esp_random();
        memcpy(&bytes[i * 4], &r, 4);
    }
    // Set version 4 and variant bits
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;

    snprintf(out, len,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3],
             bytes[4], bytes[5],
             bytes[6], bytes[7],
             bytes[8], bytes[9],
             bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
}

// Load or generate clientId from NVS
static void ensure_client_id(void)
{
    if (s_client_id[0] != '\0') return;  // Already loaded

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS");
        generate_uuid(s_client_id, sizeof(s_client_id));
        return;
    }

    size_t len = sizeof(s_client_id);
    if (nvs_get_str(nvs, NVS_KEY_ID, s_client_id, &len) == ESP_OK) {
        ESP_LOGI(TAG, "Loaded clientId from NVS: %s", s_client_id);
    } else {
        generate_uuid(s_client_id, sizeof(s_client_id));
        nvs_set_str(nvs, NVS_KEY_ID, s_client_id);
        nvs_commit(nvs);
        ESP_LOGI(TAG, "Generated new clientId: %s", s_client_id);
    }
    nvs_close(nvs);
}

// Load token from NVS (returns true if found)
static bool load_token(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return false;

    size_t len = sizeof(s_token);
    bool ok = (nvs_get_str(nvs, NVS_KEY_TOK, s_token, &len) == ESP_OK && len > 1);
    nvs_close(nvs);

    if (ok) ESP_LOGI(TAG, "Loaded token from NVS (%d bytes)", (int)len);
    return ok;
}

// Store token in NVS
static bool store_token(const char *token)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) return false;

    esp_err_t err = nvs_set_str(nvs, NVS_KEY_TOK, token);
    if (err == ESP_OK) nvs_commit(nvs);
    nvs_close(nvs);

    return err == ESP_OK;
}

// ---------------------------------------------------------------------------
// HTTP response buffer for esp_http_client
// ---------------------------------------------------------------------------

struct HttpBuf {
    char  *data;
    size_t len;
    size_t cap;
};

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    HttpBuf *buf = (HttpBuf *)evt->user_data;
    if (!buf) return ESP_OK;

    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (buf->len + evt->data_len < buf->cap) {
            memcpy(buf->data + buf->len, evt->data, evt->data_len);
            buf->len += evt->data_len;
            buf->data[buf->len] = '\0';
        }
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Auth task — runs POST + poll in background
// ---------------------------------------------------------------------------

static void auth_task(void *arg)
{
    ESP_LOGI(TAG, "Auth task started");
    s_error[0] = '\0';
    s_state = SK_AUTH_REQUESTING;

    ensure_client_id();

    // ── Build POST body ───────────────────────────────────────────────────
    char url[256];
    snprintf(url, sizeof(url), "%s/signalk/v1/access/requests", s_base_url);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "clientId", s_client_id);
    cJSON_AddStringToObject(body, "description", "Autopilot Controller");
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    // ── POST request ──────────────────────────────────────────────────────
    char resp_buf[1024] = {};
    HttpBuf hbuf = { resp_buf, 0, sizeof(resp_buf) - 1 };

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_POST;
    cfg.event_handler = http_event_handler;
    cfg.user_data = &hbuf;
    cfg.timeout_ms = 10000;
    cfg.skip_cert_common_name_check = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body_str, strlen(body_str));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    cJSON_free(body_str);

    if (err != ESP_OK || (status != 200 && status != 202)) {
        ESP_LOGE(TAG, "POST failed: err=%d status=%d", err, status);
        ESP_LOGE(TAG, "Response: %s", resp_buf);

        if (status == 500) {
            strncpy(s_error, "Security not enabled on server.\nSet up admin at SignalK web UI.", sizeof(s_error) - 1);
        } else if (status == 401 || status == 403) {
            strncpy(s_error, "Access denied by server.", sizeof(s_error) - 1);
        } else if (err != ESP_OK) {
            strncpy(s_error, "Cannot reach SignalK server.", sizeof(s_error) - 1);
        } else {
            snprintf(s_error, sizeof(s_error), "Server error (HTTP %d).", status);
        }

        s_state = SK_AUTH_ERROR;
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "POST response (%d): %s", status, resp_buf);

    // ── Parse href from response ──────────────────────────────────────────
    cJSON *resp = cJSON_Parse(resp_buf);
    if (!resp) {
        ESP_LOGE(TAG, "Failed to parse POST response");
        s_state = SK_AUTH_ERROR;
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    const cJSON *href_json = cJSON_GetObjectItem(resp, "href");
    char href[256] = {};
    if (cJSON_IsString(href_json)) {
        strncpy(href, href_json->valuestring, sizeof(href) - 1);
    }
    cJSON_Delete(resp);

    if (href[0] == '\0') {
        ESP_LOGE(TAG, "No href in response");
        s_state = SK_AUTH_ERROR;
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Polling: %s%s", s_base_url, href);

    // ── Poll for approval ─────────────────────────────────────────────────
    char poll_url[384];
    snprintf(poll_url, sizeof(poll_url), "%s%s", s_base_url, href);

    for (int attempt = 0; attempt < 60; attempt++) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        hbuf.len = 0;
        resp_buf[0] = '\0';

        esp_http_client_config_t pcfg = {};
        pcfg.url = poll_url;
        pcfg.method = HTTP_METHOD_GET;
        pcfg.event_handler = http_event_handler;
        pcfg.user_data = &hbuf;
        pcfg.timeout_ms = 10000;
        pcfg.skip_cert_common_name_check = true;

        esp_http_client_handle_t pc = esp_http_client_init(&pcfg);
        err = esp_http_client_perform(pc);
        status = esp_http_client_get_status_code(pc);
        esp_http_client_cleanup(pc);

        if (err != ESP_OK || status != 200) {
            ESP_LOGW(TAG, "Poll error: err=%d status=%d", err, status);
            continue;
        }

        cJSON *pr = cJSON_Parse(resp_buf);
        if (!pr) continue;

        const cJSON *state_json = cJSON_GetObjectItem(pr, "state");
        if (!cJSON_IsString(state_json)) {
            cJSON_Delete(pr);
            continue;
        }

        if (strcmp(state_json->valuestring, "COMPLETED") == 0) {
            // Look for token in accessRequest.permission == "APPROVED"
            const cJSON *ar = cJSON_GetObjectItem(pr, "accessRequest");
            const cJSON *perm = ar ? cJSON_GetObjectItem(ar, "permission") : NULL;
            const cJSON *tok  = ar ? cJSON_GetObjectItem(ar, "token") : NULL;

            if (cJSON_IsString(perm) && strcmp(perm->valuestring, "APPROVED") == 0
                && cJSON_IsString(tok))
            {
                strncpy(s_token, tok->valuestring, sizeof(s_token) - 1);
                store_token(s_token);
                ESP_LOGI(TAG, "Access APPROVED, token stored");
                s_state = SK_AUTH_APPROVED;
            } else {
                ESP_LOGW(TAG, "Access DENIED");
                strncpy(s_error, "Admin denied the request.", sizeof(s_error) - 1);
                s_state = SK_AUTH_DENIED;
            }
            cJSON_Delete(pr);
            s_task = NULL;
            vTaskDelete(NULL);
            return;
        }

        cJSON_Delete(pr);
        ESP_LOGD(TAG, "Poll %d/60: still pending", attempt + 1);
    }

    ESP_LOGW(TAG, "Polling timed out after 120s");
    strncpy(s_error, "Timed out waiting for approval.", sizeof(s_error) - 1);
    s_state = SK_AUTH_ERROR;
    s_task = NULL;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void signalk_auth_init(const char *ws_uri)
{
    derive_http_base(ws_uri, s_base_url, sizeof(s_base_url));
    ESP_LOGI(TAG, "Auth init, base URL: %s", s_base_url);

    ensure_client_id();

    if (load_token()) {
        s_state = SK_AUTH_APPROVED;
    }
}

void signalk_auth_set_base_url(const char *ws_uri)
{
    derive_http_base(ws_uri, s_base_url, sizeof(s_base_url));
    ESP_LOGI(TAG, "Base URL updated: %s", s_base_url);
}

void signalk_auth_request_access(void)
{
    if (s_task != NULL) {
        ESP_LOGW(TAG, "Auth request already in progress");
        return;
    }
    if (s_base_url[0] == '\0') {
        ESP_LOGE(TAG, "No base URL set — call signalk_auth_init() first");
        s_state = SK_AUTH_ERROR;
        return;
    }

    xTaskCreate(auth_task, "sk_auth", 6144, NULL, 5, &s_task);
}

signalk_auth_state_t signalk_auth_get_state(void)
{
    return s_state;
}

const char *signalk_auth_get_token(void)
{
    return s_token[0] ? s_token : NULL;
}

bool signalk_auth_has_token(void)
{
    return s_token[0] != '\0';
}

const char *signalk_auth_get_error(void)
{
    return s_error[0] ? s_error : NULL;
}

void signalk_auth_clear_token(void)
{
    s_token[0] = '\0';
    s_state = SK_AUTH_NONE;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_key(nvs, NVS_KEY_TOK);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    ESP_LOGI(TAG, "Token cleared");
}

bool signalk_auth_validate_token(void)
{
    if (!s_token[0] || !s_base_url[0]) return false;

    /* SignalK has no dedicated /auth/validate endpoint.
     * Validate by requesting a protected endpoint with the token. */
    char url[256];
    snprintf(url, sizeof(url), "%s/signalk/v1/api/vessels/self", s_base_url);

    char auth_header[560];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_token);

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = 5000;
    cfg.skip_cert_common_name_check = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Authorization", auth_header);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "Token validation failed: err=%d status=%d", err, status);
        if (status == 401 || status == 403) {
            signalk_auth_clear_token();
        }
        return false;
    }

    ESP_LOGI(TAG, "Token valid (HTTP %d)", status);
    return true;
}

int signalk_auth_api_call(int method, const char *path, const char *body)
{
    if (!s_token[0]) {
        ESP_LOGW(TAG, "No auth token — skipping API call: %s", path);
        return -1;
    }
    if (!s_base_url[0]) {
        ESP_LOGE(TAG, "No base URL — skipping API call");
        return -1;
    }

    char url[384];
    snprintf(url, sizeof(url), "%s%s", s_base_url, path);

    char auth_header[560];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_token);

    char resp_buf[512] = {};
    HttpBuf hbuf = { resp_buf, 0, sizeof(resp_buf) - 1 };

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = (esp_http_client_method_t)method;
    cfg.event_handler = http_event_handler;
    cfg.user_data = &hbuf;
    cfg.timeout_ms = 5000;
    cfg.skip_cert_common_name_check = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Authorization", auth_header);

    if (body) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));
    }

    ESP_LOGI(TAG, "API %s %s %s", method == HTTP_METHOD_PUT ? "PUT" : "POST", path, body ? body : "");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "API call failed: %s err=%d", path, err);
        return -1;
    }

    if (status == 401 || status == 403) {
        ESP_LOGW(TAG, "API auth rejected (%d) — clearing token", status);
        signalk_auth_clear_token();
    }

    ESP_LOGI(TAG, "API response: %d %s", status, resp_buf);
    return status;
}

const char *signalk_auth_get_base_url(void)
{
    return s_base_url[0] ? s_base_url : nullptr;
}
