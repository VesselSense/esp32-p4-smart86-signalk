#pragma once

#include "esp_ui.hpp"
#include "signalk_client.hpp"
#include "wind_gauge_widget.hpp"

// ---------------------------------------------------------------------------
// SignalKApp - esp-ui phone app showing scrollable instrument panels
//
// Layout (720x720, ~640px visible after status/nav bars):
//   Page 1 — Navigation:  SOG | COG  /  Heading | STW
//   Page 2 — Wind:        Analog AWA/TWA compass gauge + AWS | TWS speed cells
//   Page 3 — Environment: Depth | Water Temp  /  Fresh Water
//   Page 4 — Trip:        VMG | Trip Log  /  Battery V | Battery A
//
// Vertical scroll with snap-to-page.
// LVGL timer refreshes values every CONFIG_DISPLAY_UPDATE_MS.
// ---------------------------------------------------------------------------

class SignalKApp : public ESP_UI_PhoneApp {
public:
    SignalKApp();
    ~SignalKApp();

protected:
    bool run() override;
    bool back() override;
    bool pause() override;
    bool resume() override;

private:
    // One label per instrument value display
    lv_obj_t *_lbl_sog        = nullptr;
    lv_obj_t *_lbl_cog        = nullptr;
    lv_obj_t *_lbl_heading    = nullptr;
    lv_obj_t *_lbl_stw        = nullptr;
    lv_obj_t *_lbl_depth      = nullptr;
    lv_obj_t *_lbl_water_temp = nullptr;
    lv_obj_t *_lbl_vmg        = nullptr;
    lv_obj_t *_lbl_trip_log   = nullptr;
    lv_obj_t *_lbl_batt_v     = nullptr;
    lv_obj_t *_lbl_batt_a     = nullptr;
    lv_obj_t *_lbl_depth_keel = nullptr;
    lv_obj_t *_lbl_status     = nullptr;  // connection status dot

    // Wind vane gauge (Page 2) — handles managed by wind_gauge_widget
    WindGaugeHandles _wind;

    lv_timer_t *_update_timer = nullptr;

    // Build UI in run()
    void build_ui(lv_obj_t *screen);
    lv_obj_t *create_page(lv_obj_t *scroller, int32_t w, int32_t h);
    void create_wind_page(lv_obj_t *scroller, int32_t w, int32_t h);

    // Called by LVGL timer
    void update_display();
    static void on_update_timer(lv_timer_t *t);

    // Format helpers (signalk_app-specific conversions)
    static void fmt_degrees(char *buf, size_t len, const InstrumentValue &v);
    static void fmt_meters(char *buf, size_t len, const InstrumentValue &v);
    static void fmt_celsius(char *buf, size_t len, const InstrumentValue &v);
    static void fmt_volts(char *buf, size_t len, const InstrumentValue &v);
    static void fmt_amps(char *buf, size_t len, const InstrumentValue &v);
    static void fmt_nm(char *buf, size_t len, const InstrumentValue &v);
    static void fmt_wind_angle(char *buf, size_t len, const InstrumentValue &v);
};
