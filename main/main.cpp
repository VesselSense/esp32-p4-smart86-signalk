/*
 * SignalK Instrument Panel - Waveshare ESP32-P4-WIFI6-Touch-LCD-4B
 * 4" 720x720 IPS MIPI-DSI display, GT911 touch, ST7703 driver
 *
 * Architecture:
 *   - BSP handles display/touch init (ST7703 + GT911 via Waveshare BSP)
 *   - esp-ui (esp-brookesia v0.2.x) provides phone launcher shell
 *   - SignalKApp is the instrument panel app installed in the launcher
 *   - SignalKClient manages WebSocket connection in a FreeRTOS task
 *   - WiFi via ESP32-C6 coprocessor (esp-hosted/wifi_remote)
 *   - mDNS discovers SignalK servers on the local network
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "mdns.h"
#include <time.h>
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "esp_ui.hpp"
#include "systems/phone/stylesheet/dark/phone_stylesheet.h"
#include "data_browser_app.hpp"
#include "autopilot_app.hpp"
#include "wind_rose_app.hpp"
#include "wifi_settings_app.hpp"
#include "settings_app.hpp"
#include "unit_config.hpp"
#include "signalk_client.hpp"
#include "signalk_auth.hpp"
#include "wifi_manager.hpp"
#include "audio_feedback.hpp"
#include "sk_logo_alpha.h"

static const char *TAG = "main";

// Forward declaration — defined below WiFi section
static const char *discover_signalk_uri(void);

// ---------------------------------------------------------------------------
// Launcher status overlay — SK logo + connection text on the home screen
// Uses LV_IMG_CF_ALPHA_8BIT so the logo is fully transparent where alpha=0,
// blending naturally with whatever the launcher background color is.
// ---------------------------------------------------------------------------

static lv_obj_t *s_status_img   = nullptr;
static lv_obj_t *s_status_label = nullptr;
static ESP_UI_Phone *s_phone    = nullptr;

static const lv_color_t CLR_GREEN = LV_COLOR_MAKE(0x00, 0xc8, 0x53);
static const lv_color_t CLR_RED   = LV_COLOR_MAKE(0xe0, 0x40, 0x40);
static const lv_color_t CLR_AMBER = LV_COLOR_MAKE(0xff, 0xa0, 0x40);
static const lv_color_t CLR_LABEL = LV_COLOR_MAKE(0xcc, 0xcc, 0xcc);

/* LVGL image descriptor pointing to the alpha map data */
static const lv_img_dsc_t sk_logo_dsc = {
    .header = {
        .cf          = LV_IMG_CF_ALPHA_8BIT,
        .always_zero = 0,
        .reserved    = 0,
        .w           = SK_LOGO_WIDTH,
        .h           = SK_LOGO_HEIGHT,
    },
    .data_size = SK_LOGO_WIDTH * SK_LOGO_HEIGHT,
    .data      = (const uint8_t *)sk_logo_alpha,
};


static void update_status_overlay(signalk_conn_state_t state)
{
    lv_color_t icon_color;
    const char *text;

    switch (state) {
        case SK_STATE_WIFI_CONNECTING:
            icon_color = CLR_AMBER;
            text = "Connecting WiFi...";
            break;
        case SK_STATE_MDNS_SEARCHING:
            icon_color = CLR_AMBER;
            text = "Searching for SignalK...";
            break;
        case SK_STATE_WS_CONNECTING:
            icon_color = CLR_AMBER;
            text = "Connecting...";
            break;
        case SK_STATE_CONNECTED:
            icon_color = CLR_GREEN;
            text = "Connected";
            break;
        case SK_STATE_DISCONNECTED:
        default:
            icon_color = CLR_RED;
            text = "Disconnected";
            break;
    }

    if (s_status_img) {
        lv_obj_set_style_img_recolor(s_status_img, icon_color, 0);
        lv_obj_set_style_img_recolor_opa(s_status_img, LV_OPA_COVER, 0);
    }
    if (s_status_label) {
        lv_label_set_text(s_status_label, text);
    }
}

// ---------------------------------------------------------------------------
// Status bar — WiFi icon + clock
// ---------------------------------------------------------------------------

static void update_status_bar_wifi(void)
{
    if (!s_phone) return;

    int state = 0;  // 0=closed, 1-3=signal levels
    if (wifi_manager_get_state() == WIFI_MGR_CONNECTED) {
        // Get RSSI for connected network from scan results
        WifiScanResult results[WIFI_MGR_MAX_SCAN];
        int count = wifi_manager_scan_get_results(results, WIFI_MGR_MAX_SCAN);
        const char *ssid = wifi_manager_get_ssid();
        int rssi = -100;
        for (int i = 0; i < count; i++) {
            if (strcmp(results[i].ssid, ssid) == 0) {
                rssi = results[i].rssi;
                break;
            }
        }
        // Map RSSI to 3 levels
        if (rssi > -50)       state = 3;  // excellent
        else if (rssi > -70)  state = 2;  // good
        else                  state = 1;  // weak
    }

    s_phone->getHome().getStatusBar()->setWifiIconState(state);
}

static void update_status_bar_clock(void)
{
    if (!s_phone) return;

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // Skip if time hasn't been synced yet (year < 2024)
    if (timeinfo.tm_year < (2024 - 1900)) return;

    bool is_pm = (timeinfo.tm_hour >= 12);
    int hour12 = timeinfo.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    s_phone->getHome().getStatusBar()->setClock(hour12, timeinfo.tm_min, is_pm);
}

static void init_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP...");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    /* Timezone already applied by unit_config_init() at boot */
}

static void status_timer_cb(lv_timer_t *t)
{
    // Check if max retries reached and client needs re-discovery
    if (signalk_client_needs_rediscovery()) {
        ESP_LOGI(TAG, "Tearing down old client and re-discovering via mDNS...");
        signalk_client_stop();
        const char *uri = discover_signalk_uri();
        if (uri) {
            signalk_client_start(uri);
            signalk_auth_set_base_url(uri);
        } else {
            ESP_LOGW(TAG, "mDNS re-discovery failed — will retry next cycle");
            signalk_client_set_state(SK_STATE_DISCONNECTED);
        }
    }

    /* Also try initial discovery if WiFi is connected but SignalK never found */
    if (signalk_client_get_state() == SK_STATE_DISCONNECTED &&
        wifi_manager_get_state() == WIFI_MGR_CONNECTED &&
        !signalk_client_is_connected()) {
        const char *uri = discover_signalk_uri();
        if (uri) {
            signalk_client_start(uri);
            signalk_auth_init(uri);
        }
    }

    static signalk_conn_state_t prev = (signalk_conn_state_t)-1;
    signalk_conn_state_t cur = signalk_client_get_state();
    if (cur != prev) {
        prev = cur;
        update_status_overlay(cur);
    }

    // Update status bar WiFi icon and clock
    update_status_bar_wifi();
    update_status_bar_clock();
}

// ---------------------------------------------------------------------------
// mDNS discovery
// ---------------------------------------------------------------------------

static char s_discovered_uri[128];

static const char *discover_signalk_uri(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(err));
        return NULL;
    }

    ESP_LOGI(TAG, "mDNS: searching for _signalk-ws._tcp ...");
    signalk_client_set_state(SK_STATE_MDNS_SEARCHING);

    mdns_result_t *results = NULL;
    err = mdns_query_ptr("_signalk-ws", "_tcp",
                          CONFIG_SIGNALK_MDNS_TIMEOUT_MS, 5, &results);
    if (err != ESP_OK || results == NULL) {
        ESP_LOGW(TAG, "mDNS: no SignalK server found on network");
        mdns_free();
        return NULL;
    }

    // Walk the result list — use first one with an IPv4 address
    for (mdns_result_t *r = results; r != NULL; r = r->next) {
        for (mdns_ip_addr_t *a = r->addr; a != NULL; a = a->next) {
            if (a->addr.type == ESP_IPADDR_TYPE_V4) {
                snprintf(s_discovered_uri, sizeof(s_discovered_uri),
                         "ws://" IPSTR ":%u/signalk/v1/stream?subscribe=none",
                         IP2STR(&a->addr.u_addr.ip4), r->port);
                ESP_LOGI(TAG, "mDNS discovered SignalK: %s (host=%s)",
                         s_discovered_uri, r->hostname ? r->hostname : "?");
                mdns_query_results_free(results);
                return s_discovered_uri;
            }
        }
    }

    ESP_LOGW(TAG, "mDNS: found service but no IPv4 address");
    mdns_query_results_free(results);
    return NULL;
}

// ---------------------------------------------------------------------------
// WiFi — delegated to wifi_manager component
// ---------------------------------------------------------------------------

static void on_wifi_got_ip(void)
{
    // Always stop + re-discover on WiFi connect (IP may have changed)
    signalk_client_stop();
    const char *uri = discover_signalk_uri();
    if (!uri) {
        ESP_LOGW(TAG, "No SignalK server found — will retry via status timer");
        signalk_client_set_state(SK_STATE_DISCONNECTED);
        return;
    }
    signalk_client_start(uri);
    signalk_auth_init(uri);

    // Start SNTP time sync (safe to call multiple times — checks if already running)
    if (!esp_sntp_enabled()) {
        init_sntp();
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "SignalK Instrument Panel starting");

    // --- BSP: I2C, display, touch ---
    bsp_i2c_init();

    bsp_display_cfg_t cfg = {};
    cfg.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    cfg.buffer_size   = BSP_LCD_H_RES * 40;   // 40-line draw buffer in PSRAM
    cfg.flags.buff_spiram = true;
    lv_disp_t *disp = bsp_display_start_with_config(&cfg);
    ESP_ERROR_CHECK(disp == NULL ? ESP_FAIL : ESP_OK);
    bsp_display_backlight_on();

    ESP_LOGI(TAG, "Display ready: %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);

    // --- Audio feedback ---
    audio_feedback_init();

    // --- WiFi (wifi_manager handles init, events, NVS) ---
    signalk_client_set_state(SK_STATE_WIFI_CONNECTING);
    wifi_manager_init(on_wifi_got_ip);

    // --- Unit config (timezone, speed, depth, temp, distance — from NVS) ---
    // Must be after wifi_manager_init() which calls nvs_flash_init()
    unit_config_init();

    // Connect to best saved network in range, or stay disconnected
    if (!wifi_manager_connect_saved()) {
        ESP_LOGI(TAG, "No saved networks in range — use WiFi app to configure");
    }

    // --- esp-ui phone shell ---
    bsp_display_lock(0);

    ESP_LOGI(TAG, "LVGL disp size: %dx%d", lv_disp_get_hor_res(disp), lv_disp_get_ver_res(disp));

    ESP_UI_Phone *phone = new ESP_UI_Phone(disp);
    ESP_UI_CHECK_NULL_EXIT(phone, "Create phone failed");
    s_phone = phone;

    // Workaround: esp-ui v0.2.1 bug — calibrateResolutionSize resolves percent→pixels
    // in a local copy, so calibrateStylesheet receives screen_size with raw height=0.
    // Fix: add stylesheet with explicit pixel dimensions before begin().
    ESP_UI_PhoneStylesheet_t stylesheet = ESP_UI_PHONE_DEFAULT_DARK_STYLESHEET();
    stylesheet.core.screen_size = ESP_UI_STYLE_SIZE_RECT(BSP_LCD_H_RES, BSP_LCD_V_RES);
    ESP_UI_CHECK_FALSE_EXIT(phone->addStylesheet(stylesheet), "Add stylesheet failed");

    ESP_UI_CHECK_FALSE_EXIT(phone->setTouchDevice(bsp_display_get_input_dev()),
                            "Set touch device failed");
    ESP_UI_CHECK_FALSE_EXIT(phone->begin(), "Phone begin failed");

    // --- Hide battery icon (no battery on this board) ---
    phone->getHome().getStatusBar()->hideBatteryIcon();

    // --- Status overlay on launcher home screen ---
    lv_obj_t *home_scr = phone->getHome().getMainScreen();

    s_status_img = lv_img_create(home_scr);
    lv_img_set_src(s_status_img, &sk_logo_dsc);
    lv_obj_align(s_status_img, LV_ALIGN_BOTTOM_MID, 0, -120);

    s_status_label = lv_label_create(home_scr);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status_label, CLR_LABEL, 0);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_status_label, 400);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -70);

    // Force initial amber state — WiFi connect may have already fired
    update_status_overlay(signalk_client_get_state());

    // Timer to poll state changes (500ms for responsive feedback)
    lv_timer_create(status_timer_cb, 500, NULL);

    // --- Install Settings app (first in launcher) ---
    SettingsApp *settings_app = new SettingsApp();
    ESP_UI_CHECK_NULL_EXIT(settings_app, "Create Settings app failed");
    ESP_UI_CHECK_FALSE_EXIT((phone->installApp(settings_app) >= 0),
                            "Install Settings app failed");
    ESP_LOGI(TAG, "Settings app installed in launcher");

    // --- Install Data Browser app ---
    DataBrowserApp *browser_app = new DataBrowserApp();
    ESP_UI_CHECK_NULL_EXIT(browser_app, "Create Data Browser app failed");
    ESP_UI_CHECK_FALSE_EXIT((phone->installApp(browser_app) >= 0),
                            "Install Data Browser app failed");
    ESP_LOGI(TAG, "Data Browser app installed in launcher");

    // --- Install Autopilot app ---
    AutopilotApp *ap_app = new AutopilotApp();
    ESP_UI_CHECK_NULL_EXIT(ap_app, "Create Autopilot app failed");
    ESP_UI_CHECK_FALSE_EXIT((phone->installApp(ap_app) >= 0),
                            "Install Autopilot app failed");

    ESP_LOGI(TAG, "Autopilot app installed in launcher");

    // --- Install Wind Rose app ---
    WindRoseApp *wr_app = new WindRoseApp();
    ESP_UI_CHECK_NULL_EXIT(wr_app, "Create Wind Rose app failed");
    ESP_UI_CHECK_FALSE_EXIT((phone->installApp(wr_app) >= 0),
                            "Install Wind Rose app failed");

    ESP_LOGI(TAG, "Wind Rose app installed in launcher");

    // --- Install WiFi Settings app ---
    WifiSettingsApp *wifi_app = new WifiSettingsApp();
    ESP_UI_CHECK_NULL_EXIT(wifi_app, "Create WiFi Settings app failed");
    ESP_UI_CHECK_FALSE_EXIT((phone->installApp(wifi_app) >= 0),
                            "Install WiFi Settings app failed");

    ESP_LOGI(TAG, "WiFi Settings app installed in launcher");

    bsp_display_unlock();

    // Main task: monitor heap (debug), everything else is event-driven
    while (1) {
        ESP_LOGD(TAG, "Free heap: %lu SRAM, %lu PSRAM",
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
