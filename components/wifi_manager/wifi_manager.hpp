#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Max networks stored in NVS */
#define WIFI_MGR_MAX_SAVED 5

/* Max scan results retained */
#define WIFI_MGR_MAX_SCAN  20

/* Connection timeout (ms) */
#define WIFI_MGR_CONNECT_TIMEOUT_MS 10000

struct WifiSavedNetwork {
    char ssid[33];
    char password[65];
};

struct WifiScanResult {
    char    ssid[33];
    int8_t  rssi;
    uint8_t authmode;   /* wifi_auth_mode_t cast to uint8_t */
};

/* Connection state — polled by the settings app UI */
typedef enum {
    WIFI_MGR_DISCONNECTED,
    WIFI_MGR_CONNECTING,
    WIFI_MGR_CONNECTED,
    WIFI_MGR_CONNECT_FAILED,
    WIFI_MGR_SCANNING,
} wifi_mgr_state_t;

/* Callback for IP-got event — main.cpp registers this for mDNS discovery */
typedef void (*wifi_mgr_on_ip_cb_t)(void);

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise WiFi subsystem (NVS, netif, event loop, esp_wifi).
 * Call once from app_main before any other wifi_manager calls.
 * on_ip_cb is called from the event handler when IP is obtained. */
void wifi_manager_init(wifi_mgr_on_ip_cb_t on_ip_cb);

/* Connect using NVS saved networks (tries first saved, then Kconfig fallback).
 * Returns true if a saved network was found to try. */
bool wifi_manager_connect_saved(void);

/* Connect to a specific network. Non-blocking — poll wifi_manager_get_state().
 * Returns false if busy (scanning or already connecting). */
bool wifi_manager_connect(const char *ssid, const char *password);

/* Disconnect from current network. */
void wifi_manager_disconnect(void);

/* Start an async WiFi scan. Returns false if busy (connecting). */
bool wifi_manager_scan_start(void);

/* Copy scan results into caller buffer. Returns number copied. */
int wifi_manager_scan_get_results(WifiScanResult *out, int max);

/* Returns true if scan results are available (updated since last call to scan_start). */
bool wifi_manager_scan_done(void);

/* Connection state. */
wifi_mgr_state_t wifi_manager_get_state(void);

/* Currently connected SSID (empty string if not connected). */
const char *wifi_manager_get_ssid(void);

/* Last error message (empty if no error). Cleared on next connect attempt. */
const char *wifi_manager_get_error(void);

/* NVS saved credentials. */
int  wifi_manager_load_saved(WifiSavedNetwork *out, int max);
void wifi_manager_save_network(const char *ssid, const char *password);
void wifi_manager_forget_network(const char *ssid);

#ifdef __cplusplus
}
#endif
