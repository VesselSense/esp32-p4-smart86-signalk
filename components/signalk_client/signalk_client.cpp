#include "signalk_client.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include <string.h>
#include <math.h>
#include "lvgl.h"

static const char *TAG = "signalk";

// ---------------------------------------------------------------------------
// Observer — single path watch with LVGL thread notification
// ---------------------------------------------------------------------------

static char s_watch_path[96]          = {};
static signalk_value_cb_t s_watch_cb  = nullptr;
static void *s_watch_user_data        = nullptr;

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------

static InstrumentStore           s_data  = {};
static signalk_conn_state_t     s_state = SK_STATE_WIFI_CONNECTING;
static esp_websocket_client_handle_t s_ws = nullptr;

static int  s_disconnect_count    = 0;
static bool s_needs_rediscovery   = false;
static const int MAX_RETRY_BEFORE_REDISCOVERY = 3;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

// ---------------------------------------------------------------------------
// Initial subscriptions — paths needed by autopilot and wind rose apps
// ---------------------------------------------------------------------------

struct SubEntry {
    const char *path;
    int period_ms;
};

static const SubEntry s_initial_subs[] = {
    { "navigation.speedThroughWater",                1000 },
    { "navigation.headingMagnetic",                  1000 },
    { "navigation.headingTrue",                      1000 },
    { "environment.wind.speedApparent",               500 },
    { "environment.wind.angleApparent",               500 },
    { "environment.wind.speedTrue",                   500 },
    { "environment.wind.angleTrueWater",              500 },
    { "environment.depth.belowTransducer",           2000 },
    { "steering.rudderAngle",                         500 },
    { "navigation.courseRhumbline.crossTrackError",  2000 },
};

static const int s_initial_sub_count = sizeof(s_initial_subs) / sizeof(s_initial_subs[0]);

// ---------------------------------------------------------------------------
// Subscribe / unsubscribe helpers
// ---------------------------------------------------------------------------

static void send_subscribe_list(esp_websocket_client_handle_t client,
                                const SubEntry *subs, int count)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "context", "vessels.self");
    cJSON *arr = cJSON_AddArrayToObject(root, "subscribe");

    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "path",   subs[i].path);
        cJSON_AddNumberToObject(item, "period", subs[i].period_ms);
        cJSON_AddItemToArray(arr, item);
    }

    char *msg = cJSON_PrintUnformatted(root);
    if (msg) {
        esp_websocket_client_send_text(client, msg, strlen(msg), pdMS_TO_TICKS(2000));
        ESP_LOGI(TAG, "Subscribed to %d paths", count);
        free(msg);
    }
    cJSON_Delete(root);
}

static void send_subscribe_one(esp_websocket_client_handle_t client,
                               const char *path, int period_ms)
{
    SubEntry e = { path, period_ms };
    send_subscribe_list(client, &e, 1);
}

static void send_unsubscribe_one(esp_websocket_client_handle_t client,
                                 const char *path)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "context", "vessels.self");
    cJSON *arr = cJSON_AddArrayToObject(root, "unsubscribe");

    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "path", path);
    cJSON_AddItemToArray(arr, item);

    char *msg = cJSON_PrintUnformatted(root);
    if (msg) {
        esp_websocket_client_send_text(client, msg, strlen(msg), pdMS_TO_TICKS(2000));
        ESP_LOGI(TAG, "Unsubscribed from: %s", path);
        free(msg);
    }
    cJSON_Delete(root);
}

// Called on WS connect — subscribe to initial paths
static void send_subscribe(esp_websocket_client_handle_t client)
{
    send_subscribe_list(client, s_initial_subs, s_initial_sub_count);
}

// ---------------------------------------------------------------------------
// Delta message parser
// ---------------------------------------------------------------------------

static void parse_delta(const char *json_str, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(json_str, len);
    if (!root) return;

    cJSON *updates = cJSON_GetObjectItem(root, "updates");
    if (!cJSON_IsArray(updates)) {
        cJSON_Delete(root);
        return;
    }

    uint32_t now = now_ms();
    bool notify_watcher = false;

    xSemaphoreTake(s_data.mutex, portMAX_DELAY);

    cJSON *update;
    cJSON_ArrayForEach(update, updates) {
        cJSON *values = cJSON_GetObjectItem(update, "values");
        if (!cJSON_IsArray(values)) continue;

        cJSON *val_obj;
        cJSON_ArrayForEach(val_obj, values) {
            cJSON *path_j  = cJSON_GetObjectItem(val_obj, "path");
            cJSON *value_j = cJSON_GetObjectItem(val_obj, "value");

            if (!cJSON_IsString(path_j) || !cJSON_IsNumber(value_j)) continue;

            const char *path = path_j->valuestring;
            float value      = (float)value_j->valuedouble;

            if (isnan(value) || isinf(value)) continue;

            // Store any subscribed path — no routing needed
            if (store_find_mut(s_data, path)) {
                store_set(s_data, path, value, now);

                // Check if this is the watched path
                if (s_watch_cb && s_watch_path[0] &&
                    strcmp(path, s_watch_path) == 0) {
                    notify_watcher = true;
                }
            }
        }
    }

    // ── Derive true wind from apparent wind + boat speed when server
    //    doesn't provide it directly.
    const PathValue *tws = store_find(s_data, "environment.wind.speedTrue");
    if (!tws || !tws->valid) {
        const PathValue *aws = store_find(s_data, "environment.wind.speedApparent");
        const PathValue *awa = store_find(s_data, "environment.wind.angleApparent");
        const PathValue *stw = store_find(s_data, "navigation.speedThroughWater");

        if (aws && aws->valid && awa && awa->valid && stw && stw->valid) {
            float a = aws->value, w = awa->value, s = stw->value;
            float tws_v = sqrtf(a * a + s * s - 2.0f * a * s * cosf(w));
            float twa_v = atan2f(a * sinf(w), a * cosf(w) - s);
            store_set(s_data, "environment.wind.speedTrue",      tws_v, now);
            store_set(s_data, "environment.wind.angleTrueWater", twa_v, now);
        }
    }

    xSemaphoreGive(s_data.mutex);

    // Notify watcher OUTSIDE mutex to avoid deadlock with LVGL locks
    if (notify_watcher && s_watch_cb) {
        ESP_LOGD(TAG, "Notifying watcher for: %s", s_watch_path);
        lv_async_call((lv_async_cb_t)s_watch_cb, s_watch_user_data);
    }

    cJSON_Delete(root);
}

// ---------------------------------------------------------------------------
// WebSocket event handler
// ---------------------------------------------------------------------------

static void ws_event_handler(void *handler_args, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to SignalK");
            s_state = SK_STATE_CONNECTED;
            s_disconnect_count = 0;
            send_subscribe(s_ws);
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            s_disconnect_count++;
            ESP_LOGW(TAG, "Disconnected from SignalK (attempt %d/%d)",
                     s_disconnect_count, MAX_RETRY_BEFORE_REDISCOVERY);
            if (s_disconnect_count >= MAX_RETRY_BEFORE_REDISCOVERY) {
                ESP_LOGW(TAG, "Max retries reached — requesting mDNS re-discovery");
                s_disconnect_count = 0;
                s_needs_rediscovery = true;
                s_state = SK_STATE_MDNS_SEARCHING;
            } else {
                s_state = SK_STATE_DISCONNECTED;
            }
            break;

        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == 0x01 && data->data_len > 0) {
                parse_delta(data->data_ptr, data->data_len);
            }
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            s_state = SK_STATE_DISCONNECTED;
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void signalk_client_start(const char *uri)
{
    if (s_ws) {
        ESP_LOGW(TAG, "Client already started");
        return;
    }

    // Init shared data mutex
    if (!s_data.mutex) {
        s_data.mutex = xSemaphoreCreateMutex();
    }

    // Pre-populate store with initial subscription paths (valid=false until data arrives)
    for (int i = 0; i < s_initial_sub_count; i++) {
        if (!store_find_mut(s_data, s_initial_subs[i].path)) {
            if (s_data.count < STORE_MAX_ENTRIES) {
                PathValue *pv = &s_data.entries[s_data.count++];
                strncpy(pv->path, s_initial_subs[i].path, sizeof(pv->path) - 1);
                pv->path[sizeof(pv->path) - 1] = '\0';
                pv->valid = false;
            }
        }
    }

    s_state = SK_STATE_WS_CONNECTING;

    esp_websocket_client_config_t ws_cfg = {};
    ws_cfg.uri                        = uri;
    ws_cfg.buffer_size                = 4096;
    ws_cfg.task_stack                 = 8192;
    ws_cfg.reconnect_timeout_ms       = CONFIG_SIGNALK_RECONNECT_MS;
    ws_cfg.disable_auto_reconnect     = false;
    ws_cfg.skip_cert_common_name_check = true;

    s_ws = esp_websocket_client_init(&ws_cfg);
    if (!s_ws) {
        ESP_LOGE(TAG, "Failed to init WebSocket client");
        s_state = SK_STATE_DISCONNECTED;
        return;
    }

    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY,
                                   ws_event_handler, NULL);
    esp_websocket_client_start(s_ws);

    ESP_LOGI(TAG, "SignalK client started → %s", uri);
}

void signalk_client_stop(void)
{
    if (s_ws) {
        esp_websocket_client_stop(s_ws);
        esp_websocket_client_destroy(s_ws);
        s_ws = nullptr;
    }
    s_state = SK_STATE_DISCONNECTED;
}

bool signalk_client_is_connected(void)
{
    return s_state == SK_STATE_CONNECTED;
}

signalk_conn_state_t signalk_client_get_state(void)
{
    return s_state;
}

void signalk_client_set_state(signalk_conn_state_t state)
{
    s_state = state;
}

bool signalk_client_needs_rediscovery(void)
{
    if (s_needs_rediscovery) {
        s_needs_rediscovery = false;
        return true;
    }
    return false;
}

bool signalk_client_get_data(InstrumentStore *dest)
{
    if (!s_data.mutex) return false;
    if (xSemaphoreTake(s_data.mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    *dest = s_data;
    dest->mutex = nullptr;
    xSemaphoreGive(s_data.mutex);
    return true;
}

void signalk_client_subscribe(const char *path, int period_ms)
{
    if (!s_data.mutex) return;

    xSemaphoreTake(s_data.mutex, portMAX_DELAY);
    // Add slot if not already present
    if (!store_find_mut(s_data, path)) {
        if (s_data.count < STORE_MAX_ENTRIES) {
            PathValue *pv = &s_data.entries[s_data.count++];
            strncpy(pv->path, path, sizeof(pv->path) - 1);
            pv->path[sizeof(pv->path) - 1] = '\0';
            pv->valid = false;
            ESP_LOGI(TAG, "Added store slot [%d]: %s", s_data.count - 1, path);
        } else {
            ESP_LOGW(TAG, "Store full, cannot add: %s", path);
        }
    }
    xSemaphoreGive(s_data.mutex);

    if (s_ws && s_state == SK_STATE_CONNECTED) {
        send_subscribe_one(s_ws, path, period_ms);
    } else {
        ESP_LOGW(TAG, "WS not connected, subscribe queued: %s (state=%d)", path, s_state);
    }
}

void signalk_client_unsubscribe(const char *path)
{
    // Don't remove from store — path may be shared with initial subscriptions
    // (autopilot, wind rose). Server continues sending deltas; parse_delta()
    // stores them harmlessly. The watch is cleared separately by the caller.
    ESP_LOGI(TAG, "Unwatch: %s (slot kept in store)", path);
}

void signalk_client_watch(const char *path, signalk_value_cb_t cb, void *user_data)
{
    if (path) {
        strncpy(s_watch_path, path, sizeof(s_watch_path) - 1);
        s_watch_path[sizeof(s_watch_path) - 1] = '\0';
    } else {
        s_watch_path[0] = '\0';
    }
    s_watch_cb = cb;
    s_watch_user_data = user_data;
}
