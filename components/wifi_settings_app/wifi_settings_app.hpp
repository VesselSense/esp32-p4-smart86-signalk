#pragma once

#include "esp_ui.hpp"
#include "lvgl.h"
#include "wifi_manager.hpp"

class WifiSettingsApp : public ESP_UI_PhoneApp {
public:
    WifiSettingsApp();
    ~WifiSettingsApp() override;

    bool run()    override;
    bool back()   override { return notifyCoreClosed(); }
    bool pause()  override { return true; }
    bool resume() override { return true; }

protected:
    void build_ui();
    void show_connected_view();
    void show_disconnected_view();
    void update_autocomplete();
    void hide_autocomplete();
    void show_known_networks();
    void refresh_scan_results();
    void set_status(const char *text, lv_color_t color);

    static void on_ssid_changed(lv_event_t *e);
    static void on_ssid_focused(lv_event_t *e);
    static void on_pw_focused(lv_event_t *e);
    static void on_suggestion_click(lv_event_t *e);
    static void on_connect_btn(lv_event_t *e);
    static void on_disconnect_btn(lv_event_t *e);
    static void on_known_connect(lv_event_t *e);
    static void on_poll_timer(lv_timer_t *t);

    /* Two view containers — only one visible at a time */
    lv_obj_t   *_connected_view   = nullptr;
    lv_obj_t   *_disconnected_view = nullptr;

    /* Connected view widgets */
    lv_obj_t   *_conn_ssid_lbl    = nullptr;
    lv_obj_t   *_conn_rssi_lbl    = nullptr;
    lv_obj_t   *_conn_saved_area  = nullptr;

    /* Disconnected view widgets */
    lv_obj_t   *_ssid_ta          = nullptr;
    lv_obj_t   *_pw_ta            = nullptr;
    lv_obj_t   *_keyboard         = nullptr;
    lv_obj_t   *_connect_btn      = nullptr;
    lv_obj_t   *_status_lbl       = nullptr;
    lv_obj_t   *_autocomplete     = nullptr;
    lv_obj_t   *_known_area       = nullptr;

    lv_timer_t *_poll_timer       = nullptr;

    /* Cached scan + saved results for UI */
    WifiScanResult  _scan_results[WIFI_MGR_MAX_SCAN];
    int             _scan_count = 0;

    WifiSavedNetwork _saved[WIFI_MGR_MAX_SAVED];
    int              _saved_count = 0;

    bool _scan_requested = false;
    bool _showing_connected = false;
    wifi_mgr_state_t _prev_state = (wifi_mgr_state_t)-1;

private:
};
