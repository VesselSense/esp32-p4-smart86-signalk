#include "signalk_app.hpp"
#include "signalk_client.hpp"
#include "instrument_theme.h"
#include "instrument_helpers.hpp"
#include "wind_gauge_widget.hpp"
#include "unit_config.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <math.h>

static const char *TAG = "sk_app";

LV_IMG_DECLARE(esp_ui_phone_app_launcher_image_default);

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

SignalKApp::SignalKApp()
    : ESP_UI_PhoneApp("SignalK",
                      &esp_ui_phone_app_launcher_image_default,
                      true,   // use_default_screen
                      false,  // use_status_bar
                      false)  // use_navigation_bar
{
}

SignalKApp::~SignalKApp()
{
    ESP_LOGD(TAG, "SignalKApp destroyed");
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool SignalKApp::run()
{
    ESP_LOGI(TAG, "SignalKApp::run()");

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, CLR_BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    build_ui(screen);

    // Periodic display update (runs in LVGL task - thread-safe)
    _update_timer = lv_timer_create(on_update_timer,
                                    CONFIG_DISPLAY_UPDATE_MS, this);
    return true;
}

bool SignalKApp::back()
{
    return notifyCoreClosed();
}

bool SignalKApp::pause()
{
    if (_update_timer) {
        lv_timer_pause(_update_timer);
    }
    return true;
}

bool SignalKApp::resume()
{
    if (_update_timer) {
        lv_timer_resume(_update_timer);
        lv_timer_reset(_update_timer);
    }
    return true;
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void SignalKApp::build_ui(lv_obj_t *screen)
{
    // Use visual area reported by the framework (excludes status/nav bars)
    lv_area_t vis = getVisualArea();
    int32_t w = vis.x2 - vis.x1 + 1;
    int32_t h = vis.y2 - vis.y1 + 1;

    ESP_LOGI(TAG, "Visual area: %ldx%ld at (%ld,%ld)", (long)w, (long)h,
             (long)vis.x1, (long)vis.y1);

    // Connection status dot (top-right of screen)
    _lbl_status = lv_obj_create(screen);
    lv_obj_set_size(_lbl_status, 14, 14);
    lv_obj_set_style_radius(_lbl_status, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_lbl_status, CLR_DISCONNECTED, 0);
    lv_obj_set_style_bg_opa(_lbl_status, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_lbl_status, 0, 0);
    lv_obj_align(_lbl_status, LV_ALIGN_TOP_RIGHT, -8, vis.y1 + 4);

    // ---------------------------------------------------------------------------
    // Scrollable container — snaps to each page
    // ---------------------------------------------------------------------------
    lv_obj_t *scroller = lv_obj_create(screen);
    lv_obj_set_pos(scroller, vis.x1, vis.y1);
    lv_obj_set_size(scroller, w, h);
    lv_obj_set_style_bg_color(scroller, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scroller, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scroller, 0, 0);
    lv_obj_set_style_pad_all(scroller, 0, 0);
    lv_obj_set_scroll_dir(scroller, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(scroller, LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(scroller, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(scroller, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroller, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    int32_t cell_w  = w / 2;
    int32_t cell_h  = h / 2;

    // ---------------------------------------------------------------------------
    // Page 1 — Navigation (2x2 grid)
    // ---------------------------------------------------------------------------
    lv_obj_t *page1 = create_page(scroller, w, h);
    instrument_create_cell(page1, "SOG",     unit_label(UNIT_CAT_SPEED),  cell_w, cell_h, &_lbl_sog);
    instrument_create_cell(page1, "COG",     "\xc2\xb0",   cell_w, cell_h, &_lbl_cog);
    instrument_create_cell(page1, "HEADING", "\xc2\xb0",   cell_w, cell_h, &_lbl_heading);
    instrument_create_cell(page1, "STW",     unit_label(UNIT_CAT_SPEED),  cell_w, cell_h, &_lbl_stw);

    // ---------------------------------------------------------------------------
    // Page 2 — Wind (analog vane gauge)
    // ---------------------------------------------------------------------------
    create_wind_page(scroller, w, h);

    // ---------------------------------------------------------------------------
    // Page 3 — Environment
    // ---------------------------------------------------------------------------
    lv_obj_t *page3 = create_page(scroller, w, h);
    instrument_create_cell(page3, "DEPTH",       unit_label(UNIT_CAT_DEPTH),       cell_w, cell_h, &_lbl_depth);
    instrument_create_cell(page3, "WATER TEMP",  unit_label(UNIT_CAT_TEMPERATURE), cell_w, cell_h, &_lbl_water_temp);
    instrument_create_cell(page3, "DEPTH KEEL",  unit_label(UNIT_CAT_DEPTH),       cell_w, cell_h, &_lbl_depth_keel);

    // ---------------------------------------------------------------------------
    // Page 4 — Trip (2x2 grid)
    // ---------------------------------------------------------------------------
    lv_obj_t *page4 = create_page(scroller, w, h);
    instrument_create_cell(page4, "VMG",       unit_label(UNIT_CAT_SPEED),    cell_w, cell_h, &_lbl_vmg);
    instrument_create_cell(page4, "TRIP LOG",  unit_label(UNIT_CAT_DISTANCE), cell_w, cell_h, &_lbl_trip_log);
    instrument_create_cell(page4, "VOLTAGE",   "V",    cell_w, cell_h, &_lbl_batt_v);
    instrument_create_cell(page4, "CURRENT",   "A",    cell_w, cell_h, &_lbl_batt_a);
}

lv_obj_t *SignalKApp::create_page(lv_obj_t *scroller, int32_t w, int32_t h)
{
    lv_obj_t *page = lv_obj_create(scroller);
    lv_obj_set_size(page, w, h);
    lv_obj_set_style_bg_color(page, CLR_BG, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_pad_gap(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return page;
}

void SignalKApp::create_wind_page(lv_obj_t *scroller, int32_t w, int32_t h)
{
    lv_obj_t *page = lv_obj_create(scroller);
    lv_obj_set_size(page, w, h);
    lv_obj_set_style_bg_color(page, CLR_BG, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_pad_gap(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    _wind = wind_gauge_build(page, w, h);
}

// ---------------------------------------------------------------------------
// Display update (called by LVGL timer - already in LVGL task context)
// ---------------------------------------------------------------------------

void SignalKApp::on_update_timer(lv_timer_t *t)
{
    static_cast<SignalKApp *>(t->user_data)->update_display();
}

void SignalKApp::update_display()
{
    // Update connection status dot
    bool connected = signalk_client_is_connected();
    lv_obj_set_style_bg_color(_lbl_status,
                               connected ? CLR_CONNECTED : CLR_DISCONNECTED, 0);

    // Snapshot data outside the mutex as quickly as possible
    InstrumentData d;
    if (!signalk_client_get_data(&d)) return;

    char buf[16];

    if (instrument_is_stale(d.sog)) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%.1f", unit_convert(UNIT_CAT_SPEED, d.sog.value));
    lv_label_set_text(_lbl_sog, buf);
    lv_obj_set_style_text_color(_lbl_sog,
        d.sog.valid ? CLR_VALUE : CLR_STALE, 0);

    fmt_degrees(buf, sizeof(buf), d.cog_true);
    lv_label_set_text(_lbl_cog, buf);
    lv_obj_set_style_text_color(_lbl_cog,
        d.cog_true.valid ? CLR_VALUE : CLR_STALE, 0);

    {
        const InstrumentValue &hdg = d.heading_mag.valid ? d.heading_mag : d.heading_true;
        fmt_degrees(buf, sizeof(buf), hdg);
        lv_label_set_text(_lbl_heading, buf);
        lv_obj_set_style_text_color(_lbl_heading,
            hdg.valid ? CLR_VALUE : CLR_STALE, 0);
    }

    if (instrument_is_stale(d.stw)) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%.1f", unit_convert(UNIT_CAT_SPEED, d.stw.value));
    lv_label_set_text(_lbl_stw, buf);
    lv_obj_set_style_text_color(_lbl_stw,
        d.stw.valid ? CLR_VALUE : CLR_STALE, 0);

    // Wind gauge (needles + speed strip)
    wind_gauge_update(_wind, d);

    if (instrument_is_stale(d.depth)) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%.1f", unit_convert(UNIT_CAT_DEPTH, d.depth.value));
    lv_label_set_text(_lbl_depth, buf);
    lv_obj_set_style_text_color(_lbl_depth,
        d.depth.valid ? CLR_VALUE : CLR_STALE, 0);

    if (instrument_is_stale(d.water_temp)) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%.1f", unit_convert(UNIT_CAT_TEMPERATURE, d.water_temp.value));
    lv_label_set_text(_lbl_water_temp, buf);
    lv_obj_set_style_text_color(_lbl_water_temp,
        d.water_temp.valid ? CLR_VALUE : CLR_STALE, 0);

    if (instrument_is_stale(d.depth_keel)) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%.1f", unit_convert(UNIT_CAT_DEPTH, d.depth_keel.value));
    lv_label_set_text(_lbl_depth_keel, buf);
    lv_obj_set_style_text_color(_lbl_depth_keel,
        d.depth_keel.valid ? CLR_VALUE : CLR_STALE, 0);

    if (instrument_is_stale(d.vmg)) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%.1f", unit_convert(UNIT_CAT_SPEED, d.vmg.value));
    lv_label_set_text(_lbl_vmg, buf);
    lv_obj_set_style_text_color(_lbl_vmg,
        d.vmg.valid ? CLR_VALUE : CLR_STALE, 0);

    if (instrument_is_stale(d.trip_log)) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%.1f", unit_convert(UNIT_CAT_DISTANCE, d.trip_log.value));
    lv_label_set_text(_lbl_trip_log, buf);
    lv_obj_set_style_text_color(_lbl_trip_log,
        d.trip_log.valid ? CLR_VALUE : CLR_STALE, 0);

    fmt_volts(buf, sizeof(buf), d.battery_v);
    lv_label_set_text(_lbl_batt_v, buf);
    lv_obj_set_style_text_color(_lbl_batt_v,
        d.battery_v.valid ? CLR_VALUE : CLR_STALE, 0);

    fmt_amps(buf, sizeof(buf), d.battery_a);
    lv_label_set_text(_lbl_batt_a, buf);
    lv_obj_set_style_text_color(_lbl_batt_a,
        d.battery_a.valid ? CLR_VALUE : CLR_STALE, 0);
}

// ---------------------------------------------------------------------------
// Format helpers (signalk_app-specific unit conversions)
// ---------------------------------------------------------------------------

void SignalKApp::fmt_degrees(char *buf, size_t len, const InstrumentValue &v)
{
    if (instrument_is_stale(v)) { snprintf(buf, len, "--"); return; }
    int deg = (int)(v.value * 57.2958f);
    deg = ((deg % 360) + 360) % 360;
    snprintf(buf, len, "%d", deg);
}

void SignalKApp::fmt_meters(char *buf, size_t len, const InstrumentValue &v)
{
    if (instrument_is_stale(v)) { snprintf(buf, len, "--"); return; }
    snprintf(buf, len, "%.1f", v.value);
}

void SignalKApp::fmt_celsius(char *buf, size_t len, const InstrumentValue &v)
{
    if (instrument_is_stale(v)) { snprintf(buf, len, "--"); return; }
    snprintf(buf, len, "%.1f", v.value - 273.15f);
}

void SignalKApp::fmt_volts(char *buf, size_t len, const InstrumentValue &v)
{
    if (instrument_is_stale(v)) { snprintf(buf, len, "--"); return; }
    snprintf(buf, len, "%.1f", v.value);
}

void SignalKApp::fmt_nm(char *buf, size_t len, const InstrumentValue &v)
{
    if (instrument_is_stale(v)) { snprintf(buf, len, "--"); return; }
    snprintf(buf, len, "%.1f", v.value / 1852.0f);
}

void SignalKApp::fmt_amps(char *buf, size_t len, const InstrumentValue &v)
{
    if (instrument_is_stale(v)) { snprintf(buf, len, "--"); return; }
    snprintf(buf, len, "%+.1f", v.value);
}

void SignalKApp::fmt_wind_angle(char *buf, size_t len, const InstrumentValue &v)
{
    if (instrument_is_stale(v)) { snprintf(buf, len, "--"); return; }
    int deg = (int)(v.value * 57.2958f);
    snprintf(buf, len, "%+d", deg);
}
