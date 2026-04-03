#include "wifi_manager.hpp"
#include <string.h>

/*
 * Stub wifi_manager for the desktop simulator.
 * Returns hardcoded scan results and saved networks.
 */

static wifi_mgr_state_t s_state = WIFI_MGR_CONNECTED;
static const char *s_ssid = "MyBoatWiFi";
static bool s_scan_done = true;

static WifiScanResult s_scan_results[] = {
    { "MyBoatWiFi",    -45, 3 },
    { "SatelliteNet",  -62, 3 },
    { "MarinaGuest",   -70, 3 },
    { "Neighbor5G",    -80, 3 },
};
static const int s_scan_count = 4;

static WifiSavedNetwork s_saved[] = {
    { "MyBoatWiFi",   "boat1234" },
    { "MarinaGuest",  "marina2024" },
};
static const int s_saved_count = 2;

void wifi_manager_init(wifi_mgr_on_ip_cb_t on_ip_cb)
{
    (void)on_ip_cb;
}

bool wifi_manager_connect_saved(void)
{
    return true;  /* pretend we connected to first saved */
}

bool wifi_manager_connect(const char *ssid, const char *password)
{
    (void)ssid;
    (void)password;
    s_state = WIFI_MGR_CONNECTED;
    return true;
}

void wifi_manager_disconnect(void)
{
    s_state = WIFI_MGR_DISCONNECTED;
}

bool wifi_manager_scan_start(void)
{
    s_scan_done = true;  /* instant in simulator */
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
    return s_ssid;
}

const char *wifi_manager_get_error(void)
{
    return "";
}

int wifi_manager_load_saved(WifiSavedNetwork *out, int max)
{
    int n = (s_saved_count < max) ? s_saved_count : max;
    memcpy(out, s_saved, n * sizeof(WifiSavedNetwork));
    return n;
}

void wifi_manager_save_network(const char *ssid, const char *password)
{
    (void)ssid;
    (void)password;
}

void wifi_manager_forget_network(const char *ssid)
{
    (void)ssid;
}
