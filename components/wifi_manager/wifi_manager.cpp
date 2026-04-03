#include "wifi_manager.hpp"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_mgr";

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static volatile wifi_mgr_state_t s_state = WIFI_MGR_DISCONNECTED;
static char s_connected_ssid[33] = {};
static char s_connecting_ssid[33] = {};
static char s_connecting_pw[65] = {};
static char s_error[64] = {};
static wifi_mgr_on_ip_cb_t s_on_ip_cb = nullptr;
static volatile bool s_intentional_disconnect = false;  /* suppress event during connect() */

static WifiScanResult s_scan_results[WIFI_MGR_MAX_SCAN];
static int s_scan_count = 0;
static volatile bool s_scan_done = false;
static volatile bool s_scanning = false;

static TimerHandle_t s_timeout_timer = nullptr;

static const char *NVS_NAMESPACE = "wifi_mgr";

// ---------------------------------------------------------------------------
// Disconnect reason → user message
// ---------------------------------------------------------------------------

static void set_error_from_reason(uint8_t reason)
{
    switch (reason) {
    case 2:  /* WIFI_REASON_AUTH_EXPIRE */
    case 15: /* WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT */
    case 204: /* WIFI_REASON_CONNECTION_FAIL (wrong password) */
        snprintf(s_error, sizeof(s_error), "Wrong password");
        break;
    case 201: /* WIFI_REASON_NO_AP_FOUND */
        snprintf(s_error, sizeof(s_error), "Network not found");
        break;
    case 3:  /* WIFI_REASON_AUTH_LEAVE */
    case 6:  /* WIFI_REASON_NOT_AUTHED */
    case 7:  /* WIFI_REASON_NOT_ASSOCED */
        snprintf(s_error, sizeof(s_error), "Authentication failed");
        break;
    case 200: /* WIFI_REASON_BEACON_TIMEOUT */
        snprintf(s_error, sizeof(s_error), "AP not responding");
        break;
    case 202: /* WIFI_REASON_ASSOC_FAIL */
        snprintf(s_error, sizeof(s_error), "Association failed");
        break;
    default:
        snprintf(s_error, sizeof(s_error), "Connection failed (reason %d)", reason);
        break;
    }
}

// ---------------------------------------------------------------------------
// Timeout timer callback
// ---------------------------------------------------------------------------

static void connect_timeout_cb(TimerHandle_t timer)
{
    if (s_state == WIFI_MGR_CONNECTING) {
        ESP_LOGW(TAG, "Connection timeout for \"%s\"", s_connecting_ssid);
        esp_wifi_disconnect();
        s_state = WIFI_MGR_CONNECT_FAILED;
        snprintf(s_error, sizeof(s_error), "Connection timed out");
    }
}

// ---------------------------------------------------------------------------
// NVS helpers
// ---------------------------------------------------------------------------

int wifi_manager_load_saved(WifiSavedNetwork *out, int max)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < WIFI_MGR_MAX_SAVED && count < max; i++) {
        char key_ssid[16], key_pw[16];
        snprintf(key_ssid, sizeof(key_ssid), "ssid%d", i);
        snprintf(key_pw, sizeof(key_pw), "pw%d", i);

        size_t ssid_len = sizeof(out[count].ssid);
        size_t pw_len = sizeof(out[count].password);

        if (nvs_get_str(nvs, key_ssid, out[count].ssid, &ssid_len) == ESP_OK &&
            nvs_get_str(nvs, key_pw, out[count].password, &pw_len) == ESP_OK) {
            count++;
        }
    }

    nvs_close(nvs);
    return count;
}

void wifi_manager_save_network(const char *ssid, const char *password)
{
    WifiSavedNetwork existing[WIFI_MGR_MAX_SAVED];
    int count = wifi_manager_load_saved(existing, WIFI_MGR_MAX_SAVED);

    int slot = -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(existing[i].ssid, ssid) == 0) {
            slot = i;
            strncpy(existing[i].password, password, sizeof(existing[i].password) - 1);
            existing[i].password[sizeof(existing[i].password) - 1] = '\0';
            break;
        }
    }

    if (slot < 0) {
        if (count < WIFI_MGR_MAX_SAVED) {
            slot = count;
            count++;
        } else {
            for (int i = 0; i < WIFI_MGR_MAX_SAVED - 1; i++) {
                existing[i] = existing[i + 1];
            }
            slot = WIFI_MGR_MAX_SAVED - 1;
        }
        strncpy(existing[slot].ssid, ssid, sizeof(existing[slot].ssid) - 1);
        existing[slot].ssid[sizeof(existing[slot].ssid) - 1] = '\0';
        strncpy(existing[slot].password, password, sizeof(existing[slot].password) - 1);
        existing[slot].password[sizeof(existing[slot].password) - 1] = '\0';
    }

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing");
        return;
    }

    nvs_erase_all(nvs);
    for (int i = 0; i < count; i++) {
        char key_ssid[16], key_pw[16];
        snprintf(key_ssid, sizeof(key_ssid), "ssid%d", i);
        snprintf(key_pw, sizeof(key_pw), "pw%d", i);
        nvs_set_str(nvs, key_ssid, existing[i].ssid);
        nvs_set_str(nvs, key_pw, existing[i].password);
    }

    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Saved network: %s (slot %d/%d)", ssid, slot, count);
}

void wifi_manager_forget_network(const char *ssid)
{
    WifiSavedNetwork existing[WIFI_MGR_MAX_SAVED];
    int count = wifi_manager_load_saved(existing, WIFI_MGR_MAX_SAVED);

    int found = -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(existing[i].ssid, ssid) == 0) {
            found = i;
            break;
        }
    }

    if (found < 0) return;

    for (int i = found; i < count - 1; i++) {
        existing[i] = existing[i + 1];
    }
    count--;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_erase_all(nvs);
    for (int i = 0; i < count; i++) {
        char key_ssid[16], key_pw[16];
        snprintf(key_ssid, sizeof(key_ssid), "ssid%d", i);
        snprintf(key_pw, sizeof(key_pw), "pw%d", i);
        nvs_set_str(nvs, key_ssid, existing[i].ssid);
        nvs_set_str(nvs, key_pw, existing[i].password);
    }
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Forgot network: %s", ssid);
}

// ---------------------------------------------------------------------------
// Event handler
// ---------------------------------------------------------------------------

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)data;
            ESP_LOGW(TAG, "WiFi disconnected reason=%d", disc->reason);

            /* Ignore disconnect triggered by wifi_manager_connect() tearing
             * down the old connection before starting the new one. */
            if (s_intentional_disconnect) {
                s_intentional_disconnect = false;
                ESP_LOGI(TAG, "Intentional disconnect (switching networks), ignoring");
                break;
            }

            if (s_state == WIFI_MGR_CONNECTING) {
                if (s_timeout_timer) xTimerStop(s_timeout_timer, 0);
                set_error_from_reason(disc->reason);
                s_state = WIFI_MGR_CONNECT_FAILED;
                ESP_LOGW(TAG, "Connection to \"%s\" failed: %s", s_connecting_ssid, s_error);
            } else {
                s_state = WIFI_MGR_DISCONNECTED;
                s_error[0] = '\0';
                esp_wifi_connect();
            }
            s_connected_ssid[0] = '\0';
            break;
        }
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi associated with AP");
            break;
        case WIFI_EVENT_SCAN_DONE: {
            uint16_t ap_count = 0;
            esp_wifi_scan_get_ap_num(&ap_count);
            if (ap_count > WIFI_MGR_MAX_SCAN) ap_count = WIFI_MGR_MAX_SCAN;

            wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(ap_count * sizeof(wifi_ap_record_t));
            if (ap_list) {
                esp_wifi_scan_get_ap_records(&ap_count, ap_list);
                s_scan_count = 0;
                for (int i = 0; i < ap_count; i++) {
                    if (ap_list[i].ssid[0] == '\0') continue;
                    strncpy(s_scan_results[s_scan_count].ssid, (const char *)ap_list[i].ssid, 32);
                    s_scan_results[s_scan_count].ssid[32] = '\0';
                    s_scan_results[s_scan_count].rssi = ap_list[i].rssi;
                    s_scan_results[s_scan_count].authmode = (uint8_t)ap_list[i].authmode;
                    s_scan_count++;
                }
                free(ap_list);
            }
            s_scanning = false;
            s_scan_done = true;
            ESP_LOGI(TAG, "Scan complete: %d networks", s_scan_count);
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        /* Stop timeout timer */
        if (s_timeout_timer) xTimerStop(s_timeout_timer, 0);
        s_state = WIFI_MGR_CONNECTED;
        s_error[0] = '\0';
        strncpy(s_connected_ssid, s_connecting_ssid, sizeof(s_connected_ssid) - 1);
        if (s_on_ip_cb) s_on_ip_cb();
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void wifi_manager_init(wifi_mgr_on_ip_cb_t on_ip_cb)
{
    s_on_ip_cb = on_ip_cb;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Create one-shot timeout timer */
    s_timeout_timer = xTimerCreate("wifi_to", pdMS_TO_TICKS(WIFI_MGR_CONNECT_TIMEOUT_MS),
                                    pdFALSE, NULL, connect_timeout_cb);

    /* Blocking scan at boot — results needed before connect_saved() */
    ESP_LOGI(TAG, "Running boot scan...");
    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden = false;
    esp_wifi_scan_start(&scan_cfg, true);   /* blocking */

    /* Harvest results (event handler won't fire for blocking scan) */
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > WIFI_MGR_MAX_SCAN) ap_count = WIFI_MGR_MAX_SCAN;

    wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(ap_count * sizeof(wifi_ap_record_t));
    if (ap_list) {
        esp_wifi_scan_get_ap_records(&ap_count, ap_list);
        s_scan_count = 0;
        for (int i = 0; i < ap_count; i++) {
            if (ap_list[i].ssid[0] == '\0') continue;
            strncpy(s_scan_results[s_scan_count].ssid, (const char *)ap_list[i].ssid, 32);
            s_scan_results[s_scan_count].ssid[32] = '\0';
            s_scan_results[s_scan_count].rssi = ap_list[i].rssi;
            s_scan_results[s_scan_count].authmode = (uint8_t)ap_list[i].authmode;
            s_scan_count++;
        }
        free(ap_list);
    }
    s_scan_done = true;
    s_scanning = false;

    ESP_LOGI(TAG, "Boot scan complete: %d networks found", s_scan_count);
    for (int i = 0; i < s_scan_count && i < 10; i++) {
        ESP_LOGI(TAG, "  [%d] \"%s\" rssi=%d", i, s_scan_results[i].ssid, s_scan_results[i].rssi);
    }
}

bool wifi_manager_connect_saved(void)
{
    WifiSavedNetwork saved[WIFI_MGR_MAX_SAVED];
    int saved_count = wifi_manager_load_saved(saved, WIFI_MGR_MAX_SAVED);

    if (saved_count == 0) {
        ESP_LOGI(TAG, "No saved networks in NVS");
        return false;
    }

    /* Find the saved network with the strongest signal in scan results.
     * Scan results are already sorted by RSSI (strongest first) from esp_wifi,
     * so first match wins. */
    for (int s = 0; s < s_scan_count; s++) {
        for (int n = 0; n < saved_count; n++) {
            if (strcmp(s_scan_results[s].ssid, saved[n].ssid) == 0) {
                ESP_LOGI(TAG, "Best saved network in range: \"%s\" (rssi=%d)",
                         saved[n].ssid, s_scan_results[s].rssi);
                return wifi_manager_connect(saved[n].ssid, saved[n].password);
            }
        }
    }

    ESP_LOGI(TAG, "No saved networks in range (%d saved, %d scanned)",
             saved_count, s_scan_count);
    return false;
}

bool wifi_manager_connect(const char *ssid, const char *password)
{
    /* Guard: reject if scanning or already connecting */
    if (s_scanning) {
        ESP_LOGW(TAG, "Cannot connect while scanning");
        return false;
    }
    if (s_state == WIFI_MGR_CONNECTING) {
        ESP_LOGW(TAG, "Already connecting to \"%s\"", s_connecting_ssid);
        return false;
    }

    ESP_LOGI(TAG, "Connecting to \"%s\"...", ssid);
    s_error[0] = '\0';
    s_connected_ssid[0] = '\0';

    strncpy(s_connecting_ssid, ssid, sizeof(s_connecting_ssid) - 1);
    s_connecting_ssid[sizeof(s_connecting_ssid) - 1] = '\0';
    strncpy(s_connecting_pw, password, sizeof(s_connecting_pw) - 1);
    s_connecting_pw[sizeof(s_connecting_pw) - 1] = '\0';

    /* Flag so disconnect event from tearing down old connection is ignored */
    s_intentional_disconnect = true;
    esp_wifi_disconnect();

    s_state = WIFI_MGR_CONNECTING;

    wifi_config_t wifi_cfg = {};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_cfg.sta.threshold.authmode = strlen(password) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_connect());

    /* Start timeout timer */
    if (s_timeout_timer) {
        xTimerReset(s_timeout_timer, 0);
    }

    return true;
}

void wifi_manager_disconnect(void)
{
    if (s_timeout_timer) xTimerStop(s_timeout_timer, 0);
    esp_wifi_disconnect();
    s_state = WIFI_MGR_DISCONNECTED;
    s_connected_ssid[0] = '\0';
}

bool wifi_manager_scan_start(void)
{
    /* Guard: reject if connecting */
    if (s_state == WIFI_MGR_CONNECTING) {
        ESP_LOGW(TAG, "Cannot scan while connecting");
        return false;
    }
    if (s_scanning) {
        ESP_LOGW(TAG, "Scan already in progress");
        return false;
    }

    s_scanning = true;
    s_scan_done = false;
    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden = false;
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Scan start failed: %s", esp_err_to_name(err));
        s_scanning = false;
        return false;
    }
    return true;
}

int wifi_manager_scan_get_results(WifiScanResult *out, int max)
{
    int n = (s_scan_count < max) ? s_scan_count : max;
    memcpy(out, s_scan_results, n * sizeof(WifiScanResult));
    return n;
}

bool wifi_manager_scan_done(void)
{
    return s_scan_done;
}

wifi_mgr_state_t wifi_manager_get_state(void)
{
    return s_state;
}

const char *wifi_manager_get_ssid(void)
{
    return s_connected_ssid;
}

const char *wifi_manager_get_error(void)
{
    return s_error;
}
