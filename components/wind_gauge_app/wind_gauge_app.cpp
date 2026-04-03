#include "wind_gauge_app.hpp"
#include "signalk_client.hpp"
#include "instrument_theme.h"
#include "wind_gauge_widget.hpp"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "wind_app";

LV_IMG_DECLARE(wind_gauge_icon);

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

WindGaugeApp::WindGaugeApp()
    : ESP_UI_PhoneApp("Wind",
                      &wind_gauge_icon,
                      true,   // use_default_screen
                      false,  // use_status_bar
                      false)  // use_navigation_bar
{
}

WindGaugeApp::~WindGaugeApp()
{
    ESP_LOGD(TAG, "WindGaugeApp destroyed");
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool WindGaugeApp::run()
{
    ESP_LOGI(TAG, "WindGaugeApp::run()");

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, CLR_BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_area_t vis = getVisualArea();
    int32_t w = vis.x2 - vis.x1 + 1;
    int32_t h = vis.y2 - vis.y1 + 1;
    ESP_LOGI(TAG, "Visual area: %ldx%ld at (%ld,%ld)",
             (long)w, (long)h, (long)vis.x1, (long)vis.y1);

    // Root container occupies the visual area (below status bar, above nav bar)
    lv_obj_t *root = lv_obj_create(screen);
    lv_obj_set_pos(root, vis.x1, vis.y1);
    lv_obj_set_size(root, w, h);
    lv_obj_set_style_bg_color(root, CLR_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_pad_gap(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    _wind = wind_gauge_build(root, w, h);

    _update_timer = lv_timer_create(on_update_timer,
                                    CONFIG_DISPLAY_UPDATE_MS, this);
    return true;
}

bool WindGaugeApp::back()
{
    return notifyCoreClosed();
}

bool WindGaugeApp::pause()
{
    if (_update_timer) lv_timer_pause(_update_timer);
    return true;
}

bool WindGaugeApp::resume()
{
    if (_update_timer) {
        lv_timer_resume(_update_timer);
        lv_timer_reset(_update_timer);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Display update
// ---------------------------------------------------------------------------

void WindGaugeApp::on_update_timer(lv_timer_t *t)
{
    WindGaugeApp *app = static_cast<WindGaugeApp *>(t->user_data);
    InstrumentData d;
    if (!signalk_client_get_data(&d)) return;
    wind_gauge_update(app->_wind, d);
}
