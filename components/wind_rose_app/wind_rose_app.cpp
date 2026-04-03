#include "wind_rose_app.hpp"
#include "instrument_theme.h"
#include "signalk_logo.h"
#include "unit_config.hpp"
#include "esp_log.h"
#include "sdkconfig.h"
#include <math.h>
#include <stdio.h>

static const char *TAG = "wind_rose";

LV_IMG_DECLARE(wind_gauge_icon);

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

WindRoseApp::WindRoseApp()
    : ESP_UI_PhoneApp("Wind",
                      &wind_gauge_icon,
                      true,   // use_default_screen
                      false,  // use_status_bar
                      false)  // use_navigation_bar
{
}

WindRoseApp::~WindRoseApp()
{
    ESP_LOGD(TAG, "WindRoseApp destroyed");
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool WindRoseApp::run()
{
    ESP_LOGI(TAG, "WindRoseApp::run()");

    lv_obj_t *screen = lv_scr_act();
    lv_area_t vis = getVisualArea();
    int32_t w = vis.x2 - vis.x1 + 1;
    int32_t h = vis.y2 - vis.y1 + 1;
    ESP_LOGI(TAG, "Visual area: %ldx%ld at (%ld,%ld)",
             (long)w, (long)h, (long)vis.x1, (long)vis.y1);

    // ── Background: vertical gradient, lighter gray top → near-black bottom ─
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x303035), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x0e0e10), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // Root container
    lv_obj_t *root = lv_obj_create(screen);
    lv_obj_set_pos(root, vis.x1, vis.y1);
    lv_obj_set_size(root, w, h);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    _cx = w / 2;
    _cy = h / 2;

    // ── Corner data panels (UNDER the wind rose) ──────────────────────────
    int32_t cell_w = 260;
    int32_t cell_h = 117;
    int32_t margin = 8;
    create_corner_cell(0, root, margin,              margin,              cell_w, cell_h);  // top-left
    create_corner_cell(1, root, w - margin - cell_w, margin,              cell_w, cell_h);  // top-right
    create_corner_cell(2, root, margin,              h - margin - cell_h, cell_w, cell_h);  // bottom-left
    create_corner_cell(3, root, w - margin - cell_w, h - margin - cell_h, cell_w, cell_h);  // bottom-right

    // Initial state: dashes until SignalK data arrives
    set_corner_cell(0, "AWA",        "---", "°");
    set_corner_cell(1, "TWA",        "---", "°");
    set_corner_cell(2, "Boat Speed", "--",  unit_label_long(UNIT_CAT_SPEED));
    set_corner_cell(3, "HDG",        "---", "°");

    // AWA title matches arrow (black — default, no override needed)
    // TWA title matches arrow (dark gray)
    lv_obj_set_style_text_color(_corners[1].lbl_title, lv_color_hex(0x444444), 0);

    // ── Fixed base ring (white + red/green — does NOT rotate) ─────────────
    int32_t meter_d = 560;
    _meter_r = meter_d / 2;  // 280

    lv_obj_t *base_ring = lv_meter_create(root);
    lv_obj_set_size(base_ring, meter_d, meter_d);
    lv_obj_set_pos(base_ring, _cx - _meter_r, _cy - _meter_r);
    lv_obj_set_style_bg_color(base_ring, lv_color_hex(0x131313), 0);
    lv_obj_set_style_bg_opa(base_ring, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(base_ring, 0, 0);
    lv_obj_set_style_pad_all(base_ring, 0, 0);

    lv_meter_scale_t *bs = lv_meter_add_scale(base_ring);
    lv_meter_set_scale_ticks(base_ring, bs, 1, 0, 0, lv_color_hex(0x000000));
    lv_meter_set_scale_range(base_ring, bs, 0, 360, 360, 270);

    // White ring (full 360°)
    lv_meter_indicator_t *white_ring =
        lv_meter_add_arc(base_ring, bs, 115, lv_color_hex(0xe8e8e8), 0);
    lv_meter_set_indicator_start_value(base_ring, white_ring, 0);
    lv_meter_set_indicator_end_value(base_ring, white_ring, 360);

    // Starboard (green): 0°–90° (right of bow)
    lv_meter_indicator_t *stbd_zone =
        lv_meter_add_arc(base_ring, bs, 18, lv_color_hex(0x0d934a), 0);
    lv_meter_set_indicator_start_value(base_ring, stbd_zone, 0);
    lv_meter_set_indicator_end_value(base_ring, stbd_zone, 90);

    // Port (red): 270°–360° (left of bow)
    lv_meter_indicator_t *port_zone =
        lv_meter_add_arc(base_ring, bs, 18, lv_color_hex(0xd82125), 0);
    lv_meter_set_indicator_start_value(base_ring, port_zone, 270);
    lv_meter_set_indicator_end_value(base_ring, port_zone, 360);

    // ── Rotating tick ring (ticks only, transparent bg — on top) ─────────
    _meter = lv_meter_create(root);
    lv_obj_set_size(_meter, meter_d, meter_d);
    lv_obj_set_pos(_meter, _cx - _meter_r, _cy - _meter_r);
    lv_obj_set_style_bg_opa(_meter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_meter, 0, 0);
    lv_obj_set_style_pad_all(_meter, 0, 0);

    _scale = lv_meter_add_scale(_meter);
    lv_meter_set_scale_ticks(_meter, _scale, 73, 2, 15,
                             lv_color_hex(0x333333));
    lv_meter_set_scale_major_ticks(_meter, _scale, 6, 3, 25,
                                   lv_color_hex(0x222222), -1000);
    lv_meter_set_scale_range(_meter, _scale, 0, 360, 360, 270);

    // ── Inner disc (on root, below the draw canvas) ────────────────────────
    int32_t disc_d = 314;
    lv_obj_t *disc = lv_obj_create(root);
    lv_obj_set_size(disc, disc_d, disc_d);
    lv_obj_set_pos(disc, _cx - disc_d / 2, _cy - disc_d / 2);
    lv_obj_set_style_radius(disc, disc_d / 2, 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(0xd3d3d3), 0);
    lv_obj_set_style_bg_grad_color(disc, lv_color_hex(0xb0b0b0), 0);
    lv_obj_set_style_bg_grad_dir(disc, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(disc, lv_color_hex(0x131313), 0);
    lv_obj_set_style_border_width(disc, 4, 0);
    lv_obj_set_style_pad_all(disc, 0, 0);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_SCROLLABLE);

    // ── Wind speed display on inner disc ────────────────────────────────────
    LV_FONT_DECLARE(lv_font_montserrat_76);

    lv_obj_t *lbl_title = lv_label_create(disc);
    lv_label_set_text(lbl_title, "Speed");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x222222), 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, -105);

    lv_obj_t *lbl_aws = lv_label_create(disc);
    lv_label_set_text(lbl_aws, "AWS");
    lv_obj_set_style_text_font(lbl_aws, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_aws, lv_color_hex(0x444444), 0);
    lv_obj_align(lbl_aws, LV_ALIGN_CENTER, 0, -85);

    // Integer part (left of dot)
    _lbl_speed_int = lv_label_create(disc);
    lv_label_set_text(_lbl_speed_int, "--");
    lv_obj_set_style_text_font(_lbl_speed_int, &lv_font_montserrat_76, 0);
    lv_obj_set_style_text_color(_lbl_speed_int, lv_color_hex(0x111111), 0);
    lv_obj_set_style_text_letter_space(_lbl_speed_int, 4, 0);
    lv_obj_update_layout(_lbl_speed_int);
    int32_t int_w = lv_obj_get_width(_lbl_speed_int);

    // Circle decimal dot at text baseline
    int32_t dot_x = 30;   // dot right of center
    int32_t dot_y = 50;
    lv_obj_t *dot = lv_obj_create(disc);
    lv_obj_set_size(dot, 14, 14);
    lv_obj_set_style_radius(dot, 7, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_align(dot, LV_ALIGN_CENTER, dot_x, dot_y);

    // Integer: right edge 10px left of dot
    lv_obj_align(_lbl_speed_int, LV_ALIGN_CENTER, dot_x - 10 - int_w / 2, -15);

    // Fraction part: left edge 10px right of dot
    _lbl_speed_frac = lv_label_create(disc);
    lv_label_set_text(_lbl_speed_frac, "-");
    lv_obj_set_style_text_font(_lbl_speed_frac, &lv_font_montserrat_76, 0);
    lv_obj_set_style_text_color(_lbl_speed_frac, lv_color_hex(0x111111), 0);
    lv_obj_update_layout(_lbl_speed_frac);
    int32_t frac_w = lv_obj_get_width(_lbl_speed_frac);
    lv_obj_align(_lbl_speed_frac, LV_ALIGN_CENTER, dot_x + 18 + frac_w / 2, -15);

    lv_obj_t *lbl_unit = lv_label_create(disc);
    lv_label_set_text(lbl_unit, unit_label_long(UNIT_CAT_SPEED));
    lv_obj_set_style_text_font(lbl_unit, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_unit, lv_color_hex(0x222222), 0);
    lv_obj_align(lbl_unit, LV_ALIGN_CENTER, dot_x + 18 + frac_w - 30, 55);

    // Pre-populate marker state so first render shows markers
    {
        static InstrumentStore d;
        if (signalk_client_get_data(&d)) {
            const PathValue *awa = store_find(d, "environment.wind.angleApparent");
            _awa_valid = awa && awa->valid;
            if (_awa_valid) _awa_deg = awa->value * 57.2958f;
            const PathValue *twa = store_find(d, "environment.wind.angleTrueWater");
            _twa_valid = twa && twa->valid;
            if (_twa_valid) _twa_deg = twa->value * 57.2958f;
        }
    }

    // ── Canvas for wind markers + lubber (on top of disc) ───────────────
    _canvas = lv_obj_create(root);
    lv_obj_set_pos(_canvas, 0, 0);
    lv_obj_set_size(_canvas, w, h);
    lv_obj_set_style_bg_opa(_canvas, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_canvas, 0, 0);
    lv_obj_set_style_pad_all(_canvas, 0, 0);
    lv_obj_clear_flag(_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(_canvas, 0);
    lv_obj_add_event_cb(_canvas, marker_draw_cb, LV_EVENT_DRAW_MAIN_END, this);
    // 1×1 placeholder so LVGL processes draw events for this object
    lv_obj_t *placeholder = lv_obj_create(_canvas);
    lv_obj_set_size(placeholder, 1, 1);
    lv_obj_set_style_bg_opa(placeholder, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(placeholder, 0, 0);

    // ── Degree + cardinal labels (children of root, repositioned on heading) ─
    _label_r = _meter_r - 45;
    const char *cardinals[] = {"N", "E", "S", "W"};
    for (int i = 0; i < LABEL_COUNT; i++) {
        int deg = i * 30;
        _label_deg[i] = deg;

        _labels[i] = lv_label_create(root);
        bool is_cardinal = (deg % 90 == 0);

        if (is_cardinal) {
            lv_label_set_text(_labels[i], cardinals[deg / 90]);
            lv_obj_set_style_text_font(_labels[i], &lv_font_montserrat_28, 0);
            lv_obj_set_style_text_color(_labels[i], lv_color_hex(0x111111), 0);
        } else {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", deg);
            lv_label_set_text(_labels[i], buf);
            lv_obj_set_style_text_font(_labels[i], &lv_font_montserrat_24, 0);
            lv_obj_set_style_text_color(_labels[i], lv_color_hex(0x333333), 0);
        }
    }

    // Initial layout at heading 0
    reposition_labels(0.0f);

    // ── TWS floating overlay (child of root, position freely) ─────────────
    {
        LV_FONT_DECLARE(lv_font_montserrat_bold_50);
        int32_t tws_w = 180;
        int32_t tws_h = 72;
        int32_t tws_x = _cx - tws_w / 2;
        int32_t tws_y = _cy + 50;

        _tws_container = lv_obj_create(root);
        lv_obj_set_pos(_tws_container, tws_x, tws_y);
        lv_obj_set_size(_tws_container, tws_w, tws_h);
        lv_obj_set_style_bg_opa(_tws_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(_tws_container, 0, 0);
        lv_obj_set_style_pad_all(_tws_container, 0, 0);
        lv_obj_clear_flag(_tws_container, LV_OBJ_FLAG_SCROLLABLE);

        _lbl_tws_title = lv_label_create(_tws_container);
        lv_label_set_text(_lbl_tws_title, "TWS");
        lv_obj_set_style_text_font(_lbl_tws_title, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_lbl_tws_title, lv_color_hex(0x444444), 0);
        lv_obj_align(_lbl_tws_title, LV_ALIGN_TOP_MID, 0, 10);

        _lbl_tws_value = lv_label_create(_tws_container);
        lv_label_set_text(_lbl_tws_value, "--");
        lv_obj_set_style_text_font(_lbl_tws_value, &lv_font_montserrat_bold_50, 0);
        lv_obj_set_style_text_color(_lbl_tws_value, lv_color_hex(0x111111), 0);
        lv_obj_align(_lbl_tws_value, LV_ALIGN_BOTTOM_MID, 0, 0);

        _lbl_tws_unit = lv_label_create(_tws_container);
        lv_label_set_text(_lbl_tws_unit, unit_label_long(UNIT_CAT_SPEED));
        lv_obj_set_style_text_font(_lbl_tws_unit, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_lbl_tws_unit, lv_color_hex(0x444444), 0);
        lv_obj_align(_lbl_tws_unit, LV_ALIGN_BOTTOM_RIGHT, -10, -5);
    }

    _update_timer = lv_timer_create(on_update_timer,
                                    CONFIG_DISPLAY_UPDATE_MS, this);
    return true;
}

// ---------------------------------------------------------------------------
// Corner data panels — generic cells for any instrument value.
// Layout: title (small, top-left), value (large, centre), unit (small, right of value).
// ---------------------------------------------------------------------------

void WindRoseApp::create_corner_cell(int idx, lv_obj_t *parent,
                                     int32_t x, int32_t y,
                                     int32_t w, int32_t h)
{
    CornerCell &c = _corners[idx];

    c.container = lv_obj_create(parent);
    lv_obj_set_pos(c.container, x, y);
    lv_obj_set_size(c.container, w, h);
    lv_obj_set_style_radius(c.container, 12, 0);
    lv_obj_set_style_bg_color(c.container, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(c.container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c.container, 0, 0);
    lv_obj_set_style_pad_left(c.container, 20, 0);
    lv_obj_set_style_pad_right(c.container, 10, 0);
    lv_obj_set_style_pad_top(c.container, 6, 0);
    lv_obj_set_style_pad_bottom(c.container, 6, 0);
    lv_obj_clear_flag(c.container, LV_OBJ_FLAG_SCROLLABLE);

    // Title — small, centred horizontally at top, black
    c.lbl_title = lv_label_create(c.container);
    lv_label_set_text(c.lbl_title, "");
    lv_obj_set_style_text_font(c.lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(c.lbl_title, lv_color_hex(0x000000), 0);
    lv_obj_align(c.lbl_title, LV_ALIGN_TOP_MID, 0, 0);

    // Value — 67px bold, vertically centred, 20px left margin
    LV_FONT_DECLARE(lv_font_montserrat_67);
    c.lbl_value = lv_label_create(c.container);
    lv_label_set_text(c.lbl_value, "--");
    lv_obj_set_style_text_font(c.lbl_value, &lv_font_montserrat_67, 0);
    lv_obj_set_style_text_color(c.lbl_value, lv_color_hex(0x000000), 0);
    lv_obj_align(c.lbl_value, LV_ALIGN_CENTER, 0, 5);

    // Unit — small, bottom-right with 50px right margin, black
    c.lbl_unit = lv_label_create(c.container);
    lv_label_set_text(c.lbl_unit, "");
    lv_obj_set_style_text_font(c.lbl_unit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(c.lbl_unit, lv_color_hex(0x000000), 0);
    lv_obj_align(c.lbl_unit, LV_ALIGN_BOTTOM_RIGHT, -50, 0);

    // Degree symbol — positioned top-right of value, hidden by default
    c.lbl_degree = lv_label_create(c.container);
    lv_label_set_text(c.lbl_degree, "°");
    lv_obj_set_style_text_font(c.lbl_degree, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(c.lbl_degree, lv_color_hex(0x000000), 0);
    lv_obj_add_flag(c.lbl_degree, LV_OBJ_FLAG_HIDDEN);
}

void WindRoseApp::set_corner_cell(int idx, const char *title,
                                  const char *value, const char *unit)
{
    CornerCell &c = _corners[idx];
    if (c.lbl_title) lv_label_set_text(c.lbl_title, title);
    if (c.lbl_value) lv_label_set_text(c.lbl_value, value);

    // "°" unit: show degree symbol top-right of value, hide normal unit
    bool is_deg = (unit && unit[0] == '\xC2' && unit[1] == '\xB0')  // UTF-8 °
               || (unit && strcmp(unit, "°") == 0);
    if (is_deg) {
        if (c.lbl_unit) lv_obj_add_flag(c.lbl_unit, LV_OBJ_FLAG_HIDDEN);
        if (c.lbl_degree) {
            lv_obj_clear_flag(c.lbl_degree, LV_OBJ_FLAG_HIDDEN);
            // Position: after the value text, top-right
            lv_obj_update_layout(c.lbl_value);
            int32_t vx = lv_obj_get_x(c.lbl_value);
            int32_t vw = lv_obj_get_width(c.lbl_value);
            lv_obj_set_pos(c.lbl_degree, vx + vw, lv_obj_get_y(c.lbl_value) - 5);
        }
    } else {
        if (c.lbl_unit)   { lv_obj_clear_flag(c.lbl_unit, LV_OBJ_FLAG_HIDDEN);
                            lv_label_set_text(c.lbl_unit, unit); }
        if (c.lbl_degree) lv_obj_add_flag(c.lbl_degree, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---------------------------------------------------------------------------
// Reposition labels for a given heading (degrees).
// ---------------------------------------------------------------------------

void WindRoseApp::reposition_labels(float heading_deg)
{
    lv_meter_set_scale_range(_meter, _scale, 0, 360, 360,
                             270 - (int32_t)heading_deg);

    for (int i = 0; i < LABEL_COUNT; i++) {
        float screen_deg = (float)_label_deg[i] - heading_deg;
        float rad = screen_deg * 3.14159265f / 180.0f;
        int32_t lx = _cx + (int32_t)(_label_r * sinf(rad));
        int32_t ly = _cy - (int32_t)(_label_r * cosf(rad));

        lv_obj_update_layout(_labels[i]);
        lv_obj_set_pos(_labels[i],
                       lx - lv_obj_get_width(_labels[i]) / 2,
                       ly - lv_obj_get_height(_labels[i]) / 2);
    }
}

bool WindRoseApp::back()
{
    return notifyCoreClosed();
}

bool WindRoseApp::pause()
{
    if (_update_timer) lv_timer_pause(_update_timer);
    return true;
}

bool WindRoseApp::resume()
{
    if (_update_timer) {
        lv_timer_resume(_update_timer);
        lv_timer_reset(_update_timer);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Wind marker draw callback — draws AWA and TWA triangles on the outer ring.
//
// Geometry follows the autopilot compass_draw_cb pattern:
// - Object covers the full screen (720×720)
// - Centre point (ctr) is the compass rose centre
// - r_inner = inside edge of the white ring band
// - Triangle base sits at r_inner, tip points outward toward r_inner + tri_h
// - Angle: 0° = ahead (12 o'clock). Convert to LVGL screen angle where
//   0° = 3 o'clock by subtracting 90°.
// ---------------------------------------------------------------------------

void WindRoseApp::marker_draw_cb(lv_event_t *e)
{
    WindRoseApp *app = static_cast<WindRoseApp *>(lv_event_get_user_data(e));
    if (!app->_awa_valid && !app->_twa_valid) return;

    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    // Centre of compass rose in absolute screen coords
    lv_point_t ctr;
    ctr.x = coords.x1 + app->_cx;
    ctr.y = coords.y1 + app->_cy;

    // Ring geometry: white band is 115px wide, outer edge at _meter_r
    int32_t r_outer = app->_meter_r;            // 280
    int32_t r_inner = r_outer - 115;             // 165 — inner edge of white band

    int32_t tri_h  = 36;   // triangle height (radial direction)
    int32_t tri_hw = 16;   // triangle half-width (tangential direction)

    // Helper: draw a triangle at the given wind angle (degrees, 0=ahead CW)
    auto draw_triangle = [&](float angle_deg, lv_color_t color) {
        // Convert compass angle (0°=top CW) to LVGL screen angle (0°=right CW)
        float theta = (angle_deg - 90.0f) * 0.0174533f;
        float cos_t = cosf(theta);
        float sin_t = sinf(theta);

        // Tangent direction (perpendicular to radial, CW)
        float tan_x = -sin_t;
        float tan_y =  cos_t;

        // Tip just below the red/green zone (18px wide), base points inward
        float tip_r  = (float)(r_outer - 18);
        float base_r = (float)(r_outer - 18 - tri_h);

        lv_point_t pts[3] = {
            // Tip (outward)
            { (lv_coord_t)(ctr.x + tip_r * cos_t),
              (lv_coord_t)(ctr.y + tip_r * sin_t) },
            // Base left
            { (lv_coord_t)(ctr.x + base_r * cos_t - tri_hw * tan_x),
              (lv_coord_t)(ctr.y + base_r * sin_t - tri_hw * tan_y) },
            // Base right
            { (lv_coord_t)(ctr.x + base_r * cos_t + tri_hw * tan_x),
              (lv_coord_t)(ctr.y + base_r * sin_t + tri_hw * tan_y) },
        };

        lv_draw_rect_dsc_t rdsc;
        lv_draw_rect_dsc_init(&rdsc);
        rdsc.bg_color = color;
        rdsc.bg_opa   = LV_OPA_COVER;
        lv_draw_polygon(draw_ctx, &rdsc, pts, 3);
    };

    // ── Orange heading line at 12 o'clock (same as autopilot) ──────────
    {
        lv_draw_line_dsc_t ld;
        lv_draw_line_dsc_init(&ld);
        ld.width = 4;
        ld.color = lv_color_hex(0xe67e22);   // orange — CLR_AP_NEEDLE
        ld.opa   = LV_OPA_COVER;

        lv_point_t p1 = { ctr.x, (lv_coord_t)(ctr.y - r_outer) };
        lv_point_t p2 = { ctr.x, (lv_coord_t)(ctr.y - r_inner) };
        lv_draw_line(draw_ctx, &ld, &p1, &p2);
    }

    // ── Lubber (bow) triangle — gray, fixed at 12 o'clock ──────────────
    {
        int32_t lub_half_w = 40;    // match autopilot
        int32_t lub_h      = 50;    // match autopilot
        int32_t lub_tip_y  = ctr.y - r_inner - 35;
        int32_t lub_base_y = lub_tip_y + lub_h;

        lv_point_t pts[3] = {
            { ctr.x,                             (lv_coord_t)(lub_tip_y) },
            { (lv_coord_t)(ctr.x - lub_half_w), (lv_coord_t)(lub_base_y) },
            { (lv_coord_t)(ctr.x + lub_half_w), (lv_coord_t)(lub_base_y) },
        };

        lv_draw_rect_dsc_t rdsc;
        lv_draw_rect_dsc_init(&rdsc);
        rdsc.bg_color = lv_color_hex(0x778899);
        rdsc.bg_opa   = LV_OPA_50;
        lv_draw_polygon(draw_ctx, &rdsc, pts, 3);
    }

    // Draw TWA first (underneath), then AWA on top
    if (app->_twa_valid) {
        draw_triangle(app->_twa_deg, lv_color_hex(0x444444));   // dark gray
    }
    if (app->_awa_valid) {
        draw_triangle(app->_awa_deg, lv_color_hex(0x000000));   // black
    }

    // ── SignalK logo — bottom of white ring, centred ──────────────────
    {
        int32_t logo_x = ctr.x - SIGNALK_LOGO_WIDTH / 2 + 4;
        int32_t logo_y = ctr.y + r_inner + 8;

        lv_draw_rect_dsc_t px_dsc;
        lv_draw_rect_dsc_init(&px_dsc);
        px_dsc.bg_opa = LV_OPA_COVER;
        px_dsc.bg_color = app->_sk_connected
                            ? lv_color_hex(0x00cc66)
                            : lv_color_hex(0xcc3333);

        for (int row = 0; row < SIGNALK_LOGO_HEIGHT; row++) {
            for (int col = 0; col < SIGNALK_LOGO_WIDTH; col++) {
                int byte_idx = col / 8;
                int bit_idx  = 7 - (col % 8);
                if (signalk_logo_bitmap[row][byte_idx] & (1 << bit_idx)) {
                    lv_area_t px = {
                        (lv_coord_t)(logo_x + col), (lv_coord_t)(logo_y + row),
                        (lv_coord_t)(logo_x + col), (lv_coord_t)(logo_y + row)
                    };
                    lv_draw_rect(draw_ctx, &px_dsc, &px);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Display update
// ---------------------------------------------------------------------------

void WindRoseApp::on_update_timer(lv_timer_t *t)
{
    WindRoseApp *app = static_cast<WindRoseApp *>(t->user_data);
    static InstrumentStore d;
    if (!signalk_client_get_data(&d)) return;

    // Rotate compass rose so current heading is at 12 o'clock
    const PathValue *hm = store_find(d, "navigation.headingMagnetic");
    const PathValue *ht = store_find(d, "navigation.headingTrue");
    if (hm && hm->valid) {
        app->reposition_labels(hm->value * 57.2958f);
    } else if (ht && ht->valid) {
        app->reposition_labels(ht->value * 57.2958f);
    }

    // Track connection state for logo color
    app->_sk_connected = signalk_client_is_connected();

    // Update wind angle state for marker draw callback
    const PathValue *awa = store_find(d, "environment.wind.angleApparent");
    app->_awa_valid = awa && awa->valid;
    if (app->_awa_valid) {
        app->_awa_deg = awa->value * 57.2958f;
    }
    const PathValue *twa = store_find(d, "environment.wind.angleTrueWater");
    app->_twa_valid = twa && twa->valid;
    if (app->_twa_valid) {
        app->_twa_deg = twa->value * 57.2958f;
    }

    // Invalidate canvas to trigger marker redraw
    if (app->_canvas) {
        lv_obj_invalidate(app->_canvas);
    }

    // Wind speed — update integer and fraction labels directly
    const PathValue *aws = store_find(d, "environment.wind.speedApparent");
    if (app->_lbl_speed_int && app->_lbl_speed_frac) {
        if (aws && aws->valid) {
            float kts = unit_convert(UNIT_CAT_SPEED, aws->value);
            int whole = (int)kts;
            int frac  = (int)((kts - whole) * 10.0f + 0.5f) % 10;
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", whole);
            lv_label_set_text(app->_lbl_speed_int, buf);
            snprintf(buf, sizeof(buf), "%d", frac);
            lv_label_set_text(app->_lbl_speed_frac, buf);
        } else {
            lv_label_set_text(app->_lbl_speed_int, "--");
            lv_label_set_text(app->_lbl_speed_frac, "-");
        }
    }

    // ── Corner panels ─────────────────────────────────────────────────────
    char buf[16];

    // [0] AWA — apparent wind angle (°), colored by port/starboard
    if (awa && awa->valid) {
        float raw = awa->value;   // radians, negative = port
        int deg = (int)(raw * 57.2958f + 0.5f);
        if (deg < 0) deg += 360;
        snprintf(buf, sizeof(buf), "%d", deg % 360);
        app->set_corner_cell(0, "AWA", buf, "°");
        lv_color_t clr = (raw < 0) ? lv_color_hex(0xd82125) : lv_color_hex(0x0d934a);
        lv_obj_set_style_text_color(app->_corners[0].lbl_value, clr, 0);
    } else {
        app->set_corner_cell(0, "AWA", "---", "°");
        lv_obj_set_style_text_color(app->_corners[0].lbl_value, lv_color_hex(0x000000), 0);
    }

    // [1] TWA — true wind angle (°), colored by port/starboard
    if (twa && twa->valid) {
        float raw = twa->value;
        int deg = (int)(raw * 57.2958f + 0.5f);
        if (deg < 0) deg += 360;
        snprintf(buf, sizeof(buf), "%d", deg % 360);
        app->set_corner_cell(1, "TWA", buf, "°");
        lv_color_t clr = (raw < 0) ? lv_color_hex(0xd82125) : lv_color_hex(0x0d934a);
        lv_obj_set_style_text_color(app->_corners[1].lbl_value, clr, 0);
    } else {
        app->set_corner_cell(1, "TWA", "---", "°");
        lv_obj_set_style_text_color(app->_corners[1].lbl_value, lv_color_hex(0x000000), 0);
    }

    // [2] Boat Speed — STW in knots
    const PathValue *stw = store_find(d, "navigation.speedThroughWater");
    if (stw && stw->valid) {
        snprintf(buf, sizeof(buf), "%.1f", unit_convert(UNIT_CAT_SPEED, stw->value));
        app->set_corner_cell(2, "Boat Speed", buf, unit_label_long(UNIT_CAT_SPEED));
    } else {
        app->set_corner_cell(2, "Boat Speed", "--", unit_label_long(UNIT_CAT_SPEED));
    }

    // [3] HDG — magnetic or true heading (°), with M/T source label
    if (hm && hm->valid) {
        snprintf(buf, sizeof(buf), "%d", (int)(hm->value * 57.2958f + 0.5f) % 360);
        app->set_corner_cell(3, "HDG", buf, "°");
        lv_label_set_text(app->_corners[3].lbl_unit, "Mag");
        lv_obj_clear_flag(app->_corners[3].lbl_unit, LV_OBJ_FLAG_HIDDEN);
    } else if (ht && ht->valid) {
        snprintf(buf, sizeof(buf), "%d", (int)(ht->value * 57.2958f + 0.5f) % 360);
        app->set_corner_cell(3, "HDG", buf, "°");
        lv_label_set_text(app->_corners[3].lbl_unit, "True");
        lv_obj_clear_flag(app->_corners[3].lbl_unit, LV_OBJ_FLAG_HIDDEN);
    } else {
        app->set_corner_cell(3, "HDG", "---", "°");
        lv_label_set_text(app->_corners[3].lbl_unit, "");
    }

    // ── TWS floating overlay ──────────────────────────────────────────────
    const PathValue *tws = store_find(d, "environment.wind.speedTrue");
    if (app->_lbl_tws_value) {
        if (tws && tws->valid) {
            snprintf(buf, sizeof(buf), "%.1f", unit_convert(UNIT_CAT_SPEED, tws->value));
            lv_label_set_text(app->_lbl_tws_value, buf);
        } else {
            lv_label_set_text(app->_lbl_tws_value, "--");
        }
    }
}
