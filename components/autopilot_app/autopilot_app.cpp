#include "autopilot_app.hpp"
#include "signalk_client.hpp"
#include "signalk_logo.h"
#include "instrument_theme.h"
#include "instrument_helpers.hpp"
#include "unit_config.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "audio_feedback.hpp"
#include "sdkconfig.h"
#include <stdio.h>
#include <math.h>

static const char *TAG = "ap_app";

static const char *AP_BASE = "/signalk/v2/api/vessels/self/autopilots/_default";

LV_IMG_DECLARE(helm_icon);
LV_FONT_DECLARE(lv_font_roboto_96);

/* ── Autopilot-specific colours ──────────────────────────────────────────── */
#define CLR_AP_ARC_PORT   lv_color_hex(0xe60203)   /* red — port arc (from reference)  */
#define CLR_AP_ARC_STBD   lv_color_hex(0x0db134)   /* green — starboard (from reference) */
#define CLR_AP_ARC_AHEAD  lv_color_hex(0xffffff)   /* pure white — ahead zone           */
#define CLR_AP_NEEDLE     lv_color_hex(0xe67e22)   /* orange heading indicator           */
#define CLR_AP_BTN_BG     lv_color_hex(0x1b2838)   /* button background                 */
#define CLR_AP_BTN_BORDER lv_color_hex(0x415a77)   /* button border                     */
#define CLR_AP_STBY       lv_color_hex(0xe74c3c)   /* STBY button accent                */
#define CLR_AP_AUTO_GRN   lv_color_hex(0x2ecc71)   /* AUTO button accent                */

/*
 * Compass range: 130° of compass shown in 180° of visual arc.
 * Heading is always at the top centre (12 o'clock = 270° in LVGL angle coords).
 * ±65° of compass is visible to port and starboard.
 */
#define COMPASS_RANGE  130.0f

/* ── Proportional constants ───────────────────────────────────────────── */
/* Band thickness ≈ 22% of outer radius (visual measurement from reference) */
#define BAND_RATIO    0.22f
/* Heading text height ≈ 40% of outer radius */
#define HDG_H_RATIO   0.40f

/* Create a styled touch button (visual only) */
static lv_obj_t *make_btn(lv_obj_t *parent, const char *text,
                            int32_t x, int32_t y, int32_t bw, int32_t bh,
                            lv_color_t bg, lv_color_t fg)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, bw, bh);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, CLR_AP_BTN_BORDER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, fg, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_36, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    return btn;
}

/* ── Constructor / destructor ──────────────────────────────────────────── */

AutopilotApp::AutopilotApp()
    : ESP_UI_PhoneApp("Autopilot",
                      &helm_icon,
                      true, false, false)
{}

AutopilotApp::~AutopilotApp() { ESP_LOGD(TAG, "~AutopilotApp"); }

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

bool AutopilotApp::run()
{
    ESP_LOGI(TAG, "AutopilotApp::run()");

    // Validate stored token on app launch — clears if server rejects
    if (signalk_auth_has_token()) {
        signalk_auth_validate_token();
    }

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, CLR_BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_area_t vis = getVisualArea();
    int32_t w = vis.x2 - vis.x1 + 1;
    int32_t h = vis.y2 - vis.y1 + 1;
    ESP_LOGI(TAG, "Visual area: %ldx%ld at (%ld,%ld)",
             (long)w, (long)h, (long)vis.x1, (long)vis.y1);

    lv_obj_t *root = lv_obj_create(screen);
    lv_obj_set_pos(root, vis.x1, vis.y1);
    lv_obj_set_size(root, w, h);
    lv_obj_set_style_bg_color(root, CLR_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(root, 0);

    build_ui(root, w, h);

    _update_timer = lv_timer_create(on_update_timer,
                                    CONFIG_DISPLAY_UPDATE_MS, this);

    // (overlay removed from simulator — triggered by button press only)

    return true;
}

bool AutopilotApp::back()  { return notifyCoreClosed(); }

bool AutopilotApp::pause()
{
    if (_update_timer) lv_timer_pause(_update_timer);
    return true;
}

bool AutopilotApp::resume()
{
    if (_update_timer) {
        lv_timer_resume(_update_timer);
        lv_timer_reset(_update_timer);
    }
    return true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Wind bar draw callback — renders SignalK logo top-right
 * ═══════════════════════════════════════════════════════════════════════════ */

void AutopilotApp::wind_bar_draw_cb(lv_event_t *e)
{
    AutopilotApp *app = static_cast<AutopilotApp *>(lv_event_get_user_data(e));
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    int32_t w = lv_area_get_width(&coords);

    /* SignalK logo — top right of white bar */
    int32_t logo_x = coords.x1 + w - SIGNALK_LOGO_WIDTH - 8;
    int32_t logo_y = coords.y1 + 2;

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

/* ═══════════════════════════════════════════════════════════════════════════
 * CUSTOM COMPASS DRAW CALLBACK
 *
 * Draws a 180° semicircular arc that scrolls with the heading:
 *   - Three coloured bands: port (red), ahead (white), starboard (green)
 *   - Outer and inner border outlines
 *   - Orange heading indicator line at 12 o'clock
 *   - Tick marks (major/mid/minor) clipped to arc bounds
 *   - Degree labels (N/E/S/W + numeric) ON the band in dark text
 *   - Small gray lubber triangle at top centre
 *
 * Proportions derived from visual reference.
 * ═══════════════════════════════════════════════════════════════════════════ */

void AutopilotApp::compass_draw_cb(lv_event_t *e)
{
    AutopilotApp *app = static_cast<AutopilotApp *>(lv_event_get_user_data(e));
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    int32_t w = lv_area_get_width(&coords);

    /* ── Geometry ──────────────────────────────────────────────────────── */
    int32_t r_outer = w / 2 - 10;                          /* 350px at 720 */
    int32_t arc_w   = (int32_t)(r_outer * BAND_RATIO);     /* ~77px        */
    int32_t r_inner = r_outer - arc_w;                      /* ~273px       */
    int32_t r_mid   = r_outer - arc_w / 2;                  /* ~311px       */

    lv_point_t ctr;
    ctr.x = coords.x1 + w / 2;
    ctr.y = coords.y1 + r_outer + 22;  /* 20px gap between wind bar and arc top */

    float hdg  = app->_heading_deg;
    float half = COMPASS_RANGE / 2.0f;

    /* compass_deg → visual angle (0°=3-o'clock CW, 270°=12-o'clock) */
    auto to_vis = [&](float cd) -> float {
        return 270.0f + (cd - hdg) * (180.0f / COMPASS_RANGE);
    };

    /* ── 1) Arc band: white base + red/green outer overlay ───────────── */
    lv_draw_arc_dsc_t ad;
    lv_draw_arc_dsc_init(&ad);
    ad.rounded = 0;
    ad.opa     = LV_OPA_COVER;

    /* Solid white base spanning full semicircle, full band width */
    ad.width = arc_w;
    ad.color = CLR_AP_ARC_AHEAD;
    lv_draw_arc(draw_ctx, &ad, &ctr, r_mid, 180, 360);

    /* Red/green overlay — outer 1/3 of band (inner 2/3 stays white for labels) */
    int32_t overlay_w = arc_w / 3;
    int32_t r_overlay = r_outer - overlay_w / 2;   /* centered in outer portion */

    ad.width = overlay_w;

    /* Red (port): left 50% = 180°–270° */
    ad.color = CLR_AP_ARC_PORT;
    lv_draw_arc(draw_ctx, &ad, &ctr, r_overlay, 180, 270);

    /* Green (stbd): right 50% = 270°–360° */
    ad.color = CLR_AP_ARC_STBD;
    lv_draw_arc(draw_ctx, &ad, &ctr, r_overlay, 270, 360);

    /* ── 2) Outer border outline only ─────────────────────────────────── */
    ad.width = 2;
    ad.color = lv_color_hex(0x333333);   /* dark outline */
    lv_draw_arc(draw_ctx, &ad, &ctr, r_outer, 180, 360);

    /* ── 3) Green heading indicator at 12 o'clock ──────────────────────── */
    {
        float theta270 = 270.0f * 0.0174533f;
        float cos270 = cosf(theta270);   /* 0 */
        float sin270 = sinf(theta270);   /* -1 */

        lv_draw_line_dsc_t old;
        lv_draw_line_dsc_init(&old);
        old.width = 4;
        old.color = lv_color_hex(0x00cc66);   /* green heading indicator */
        old.opa   = LV_OPA_COVER;

        int32_t r_line_top = r_outer - 18;  /* start below the colored zone */
        lv_point_t po1 = { (lv_coord_t)(ctr.x + r_line_top * cos270),
                            (lv_coord_t)(ctr.y + r_line_top * sin270) };
        lv_point_t po2 = { (lv_coord_t)(ctr.x + r_inner * cos270),
                            (lv_coord_t)(ctr.y + r_inner * sin270) };
        lv_draw_line(draw_ctx, &old, &po1, &po2);
    }

    /* ── 4) Ticks + labels ────────────────────────────────────────────── */
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.opa = LV_OPA_COVER;

    float start_deg = hdg - half;
    float end_deg   = hdg + half;

    for (int raw = -360; raw <= 720; raw += 5) {
        float cd = (float)raw;
        if (cd < start_deg || cd > end_deg) continue;

        float va = to_vis(cd);

        /* Clip ticks to within arc bounds (1° margin) */
        if (va < 181.0f || va > 359.0f) continue;

        float theta = va * 0.0174533f;
        float cos_t = cosf(theta);
        float sin_t = sinf(theta);

        int norm = ((raw % 360) + 360) % 360;
        bool major = (norm % 30 == 0);
        bool mid   = !major && (norm % 10 == 0);

        /* Tick length proportional to band width */
        int32_t tick_len;
        if (major)     tick_len = arc_w * 2 / 3;
        else if (mid)  tick_len = arc_w / 2;
        else           tick_len = arc_w / 3;

        int32_t r1 = r_outer - 2;    /* start just inside outer border */
        int32_t r2 = r1 - tick_len;

        ld.width = major ? 5 : (mid ? 3 : 2);
        ld.color = lv_color_hex(0x000000);  /* pure black ticks */

        lv_point_t p1 = { (lv_coord_t)(ctr.x + r1 * cos_t),
                          (lv_coord_t)(ctr.y + r1 * sin_t) };
        lv_point_t p2 = { (lv_coord_t)(ctr.x + r2 * cos_t),
                          (lv_coord_t)(ctr.y + r2 * sin_t) };
        lv_draw_line(draw_ctx, &ld, &p1, &p2);

        /* Labels at major ticks — dark text ON the band */
        if (!major) continue;
        if (va < 200.0f || va > 340.0f) continue;    /* clip at extremes */
        if (va > 258.0f && va < 282.0f) continue;    /* 24° lubber exclusion */

        char buf[8];
        switch (norm) {
            case 0:   snprintf(buf, sizeof(buf), "N"); break;
            case 90:  snprintf(buf, sizeof(buf), "E"); break;
            case 180: snprintf(buf, sizeof(buf), "S"); break;
            case 270: snprintf(buf, sizeof(buf), "W"); break;
            default:  snprintf(buf, sizeof(buf), "%d", norm); break;
        }

        /* Position label centered in the white inner strip of the band */
        int32_t lr = r_inner + arc_w / 6;
        int32_t lx = ctr.x + (int32_t)(lr * cos_t);
        int32_t ly = ctr.y + (int32_t)(lr * sin_t);

        lv_area_t la = {
            (lv_coord_t)(lx - 30), (lv_coord_t)(ly - 16),
            (lv_coord_t)(lx + 30), (lv_coord_t)(ly + 16)
        };

        lv_draw_label_dsc_t lbd;
        lv_draw_label_dsc_init(&lbd);
        lbd.color = lv_color_hex(0x000000);   /* dark text on band */
        lbd.font  = &lv_font_montserrat_28;
        lbd.align = LV_TEXT_ALIGN_CENTER;
        lv_draw_label(draw_ctx, &lbd, &la, buf, NULL);
    }

    /* ── 5) Lubber triangle — anti-aliased polygon ───────────────────── */
    {
        int32_t lub_half_w = 40;
        int32_t lub_h      = 50;
        int32_t lub_tip_y  = ctr.y - r_inner + 20;
        int32_t lub_base_y = lub_tip_y + lub_h;

        lv_point_t pts[3] = {
            { ctr.x,                             (lv_coord_t)(lub_tip_y) },   /* tip */
            { (lv_coord_t)(ctr.x - lub_half_w), (lv_coord_t)(lub_base_y) },  /* base left */
            { (lv_coord_t)(ctr.x + lub_half_w), (lv_coord_t)(lub_base_y) },  /* base right */
        };

        lv_draw_rect_dsc_t rdsc;
        lv_draw_rect_dsc_init(&rdsc);
        rdsc.bg_color = lv_color_hex(0x778899);
        rdsc.bg_opa   = LV_OPA_COVER;
        lv_draw_polygon(draw_ctx, &rdsc, pts, 3);
    }

    /* ── 6) Wind direction indicator — black triangle on inner arc edge ── */
    if (app->_wind_angle_valid && app->_ap_mode == MODE_WIND) {
        /* Wind angle is relative to heading: wind_compass = hdg + AWA */
        float wind_compass = hdg + app->_wind_angle_deg;
        float va = to_vis(wind_compass);

        /* Only draw if within visible arc range */
        if (va >= 185.0f && va <= 355.0f) {
            float theta = va * 0.0174533f;
            float cos_t = cosf(theta);
            float sin_t = sinf(theta);

            /* Tangent direction (perpendicular to radial, CW) */
            float tan_x = -sin_t;
            float tan_y =  cos_t;

            int32_t tri_h  = 36;   /* triangle height (radial) */
            int32_t tri_hw = 16;   /* triangle half-width (tangential) */

            /* Inside white band, tip points outward toward red/green */
            float base_r = (float)r_inner;
            float tip_r  = (float)(r_inner + tri_h);

            lv_point_t wpts[3] = {
                { (lv_coord_t)(ctr.x + tip_r * cos_t),
                  (lv_coord_t)(ctr.y + tip_r * sin_t) },
                { (lv_coord_t)(ctr.x + base_r * cos_t - tri_hw * tan_x),
                  (lv_coord_t)(ctr.y + base_r * sin_t - tri_hw * tan_y) },
                { (lv_coord_t)(ctr.x + base_r * cos_t + tri_hw * tan_x),
                  (lv_coord_t)(ctr.y + base_r * sin_t + tri_hw * tan_y) },
            };

            lv_draw_rect_dsc_t wdsc;
            lv_draw_rect_dsc_init(&wdsc);
            wdsc.bg_color = lv_color_hex(0x000000);
            wdsc.bg_opa   = LV_OPA_COVER;
            lv_draw_polygon(draw_ctx, &wdsc, wpts, 3);
        }
    }

    /* ── 7) Set angle marker — orange line, when engaged ────────────────── */
    if (app->_ap_engaged == ENGAGED_AUTO && app->_ap_mode != MODE_TRACK) {
        float set_compass;
        if (app->_ap_mode == MODE_COMPASS) {
            set_compass = app->_locked_heading;
        } else {
            set_compass = hdg + app->_locked_wind_angle;
        }

        float va = to_vis(set_compass);

        if (va >= 185.0f && va <= 355.0f) {
            float theta = va * 0.0174533f;
            float cos_t = cosf(theta);
            float sin_t = sinf(theta);

            /* Orange radial line spanning the full visible white band.
             * Verified by pixel scan: white = r=235 to r=312, green starts r=313.
             * White arc drawn at radius r_mid with width arc_w, LVGL lv_draw_arc
             * uses radius as OUTER edge → white spans (r_mid - arc_w) to r_mid. */
            int32_t r_line_inner = r_mid - arc_w;   /* 235 — inner edge of white band */
            int32_t r_line_outer = r_mid;            /* 312 — outer edge of white band */
            lv_draw_line_dsc_t sld;
            lv_draw_line_dsc_init(&sld);
            sld.width = 4;
            sld.color = CLR_AP_NEEDLE;   /* orange #e67e22 */
            sld.opa   = LV_OPA_COVER;

            lv_point_t sp1 = { (lv_coord_t)(ctr.x + r_line_outer * cos_t),
                               (lv_coord_t)(ctr.y + r_line_outer * sin_t) };
            lv_point_t sp2 = { (lv_coord_t)(ctr.x + r_line_inner * cos_t),
                               (lv_coord_t)(ctr.y + r_line_inner * sin_t) };
            lv_draw_line(draw_ctx, &sld, &sp1, &sp2);
        }
    }
}

/* ── UI construction ───────────────────────────────────────────────────── */

void AutopilotApp::build_ui(lv_obj_t *root, int32_t w, int32_t h)
{
    _main_page = root;

    /* Layout geometry */
    const int32_t top_pad   = 0;    /* flush to top — no padding */
    const int32_t wind_h    = 44;   /* tight around ticks */
    const int32_t btn_margin = 10;
    const int32_t btn_gap   = 6;
    const int32_t btn_cols  = 4;
    /* Buttons: height = 3/4 of width for better touch area */
    const int32_t btn_w     = (w - btn_margin * 2 - btn_gap * (btn_cols - 1)) / btn_cols;
    const int32_t btn_h     = btn_w * 3 / 4;
    const int32_t btn_total = btn_h * 2 + btn_gap * 3;
    const int32_t compass_h = h - top_pad - wind_h - btn_total;
    const int32_t btn_y0    = h - btn_total;

    /* Center ticks vertically in the bar */
    const int32_t scale_line_y = wind_h / 2;

    /* ═══════════════════════════════════════════════════════════════════════
     * WIND ANGLE BAR — white background, black text, two-row layout
     * ═════════════════════════════════════════════════════════════════════ */
    {
        _wind_bar = lv_obj_create(root);
        lv_obj_t *bar = _wind_bar;
        lv_obj_set_pos(bar, 0, top_pad);
        lv_obj_set_size(bar, w, wind_h);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xffffff), 0);  /* WHITE bg */
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);   /* no border */
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(bar, 0);

        /* Draw callback for SignalK logo */
        lv_obj_add_event_cb(bar, wind_bar_draw_cb,
                            LV_EVENT_DRAW_MAIN_END, this);

        /* Scale geometry: vertical ticks only, no horizontal line */
        int32_t sx = 100, sw = w - 200;
        int32_t tick_h    = wind_h - 8;   /* fill most of bar height */
        int32_t tick_top  = scale_line_y - tick_h / 2;

        /* Wind indicator FIRST (behind ticks in z-order) */
        _wind_indicator = lv_obj_create(bar);
        lv_obj_set_size(_wind_indicator, 0, tick_h);
        lv_obj_set_style_bg_color(_wind_indicator, lv_color_hex(0x0db134), 0);
        lv_obj_set_style_bg_opa(_wind_indicator, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(_wind_indicator, 0, 0);
        lv_obj_set_style_radius(_wind_indicator, 0, 0);
        lv_obj_set_style_pad_all(_wind_indicator, 0, 0);
        lv_obj_clear_flag(_wind_indicator, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(_wind_indicator, sx + sw / 2, tick_top);

        /* 7 uniform vertical ticks (including center) — on top of fill bar */
        for (int i = -3; i <= 3; i++) {
            int32_t tx = sx + sw / 2 + i * sw / 6;
            lv_obj_t *tk = lv_obj_create(bar);
            lv_obj_set_pos(tk, tx - 1, tick_top);
            lv_obj_set_size(tk, 2, tick_h);
            lv_obj_set_style_bg_color(tk, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(tk, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(tk, 0, 0);
            lv_obj_set_style_radius(tk, 0, 0);
            lv_obj_clear_flag(tk, LV_OBJ_FLAG_SCROLLABLE);

            /* Numbers: left of tick on port, right of tick on stbd, v-centered */
            if (i == 0) continue;
            char tb[8];
            snprintf(tb, sizeof(tb), "%d", abs(i) * 10);
            lv_obj_t *tl = lv_label_create(bar);
            lv_label_set_text(tl, tb);
            lv_obj_set_style_text_color(tl, lv_color_hex(0x000000), 0);
            lv_obj_set_style_text_font(tl, &lv_font_montserrat_14, 0);
            lv_obj_refr_size(tl);
            int32_t lbl_w = lv_obj_get_width(tl);
            int32_t lbl_h = lv_obj_get_height(tl);
            int32_t lbl_x = (i < 0) ? tx + 4 : tx - lbl_w - 4;  /* port: right of tick, stbd: left of tick */
            int32_t lbl_y = scale_line_y - lbl_h / 2;             /* v-centered in bar */
            lv_obj_set_pos(tl, lbl_x, lbl_y);
        }

        _wind_bar_cx = sx + sw / 2;
        _wind_bar_hw = sw / 2;
    }

    /* ═══════════════════════════════════════════════════════════════════════
     * SCROLLING COMPASS (custom-drawn 180° semicircular arc)
     * ═════════════════════════════════════════════════════════════════════ */
    {
        _compass = lv_obj_create(root);
        lv_obj_set_pos(_compass, 0, top_pad + wind_h);
        lv_obj_set_size(_compass, w, compass_h);
        lv_obj_set_style_bg_opa(_compass, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(_compass, 0, 0);
        lv_obj_set_style_pad_all(_compass, 0, 0);
        lv_obj_clear_flag(_compass, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(_compass, 0);

        /* Register custom draw callback */
        lv_obj_add_event_cb(_compass, compass_draw_cb,
                            LV_EVENT_DRAW_MAIN_END, this);

        /* ── Pilot mode label — top-left of compass area ──────────── */
        _lbl_pilot_mode = lv_label_create(_compass);
        lv_label_set_text(_lbl_pilot_mode, "Compass");
        lv_obj_set_style_text_color(_lbl_pilot_mode, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(_lbl_pilot_mode, &lv_font_montserrat_48, 0);
        lv_obj_set_pos(_lbl_pilot_mode, 8, 6);

        _lbl_wind_val = lv_label_create(_compass);
        lv_label_set_text(_lbl_wind_val, "--");
        lv_obj_set_style_text_color(_lbl_wind_val, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(_lbl_wind_val, &lv_font_montserrat_48, 0);
        lv_obj_set_width(_lbl_wind_val, w - 16);
        lv_obj_set_style_text_align(_lbl_wind_val, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(_lbl_wind_val, 8, 6);

        /* ── Heading and labels positioned relative to arc geometry ──── */
        int32_t r_outer   = w / 2 - 10;                        /* 350 */
        int32_t arc_w_px  = (int32_t)(r_outer * BAND_RATIO);   /* ~77 */
        int32_t r_inner   = r_outer - arc_w_px;                 /* ~273 */
        int32_t arc_ctr_y = r_outer + 22;                       /* matches draw cb */

        /* 144px Roboto renders ~110px tall.
         * Centre heading vertically in the arc interior. */
        int32_t font_h      = 110;
        int32_t interior_top = arc_ctr_y - r_inner;          /* inner edge at 12 o'clock */
        int32_t heading_y    = (interior_top + arc_ctr_y) / 2 - font_h / 2 + 36;

        /* Large heading number */
        _lbl_heading = lv_label_create(_compass);
        lv_label_set_text(_lbl_heading, "---\xc2\xb0");
        lv_obj_set_style_text_color(_lbl_heading, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(_lbl_heading, &lv_font_roboto_96, 0);
        lv_obj_set_pos(_lbl_heading, 12, heading_y);
        lv_obj_set_width(_lbl_heading, w);
        lv_obj_set_style_text_align(_lbl_heading, LV_TEXT_ALIGN_CENTER, 0);

        /* LH / Mag / Track — fixed near bottom of arc interior, independent of heading */
        int32_t label_y = arc_ctr_y - 24;   /* just above arc center line */

        /* LH — left side */
        _lbl_lh = lv_label_create(_compass);
        lv_label_set_text(_lbl_lh, "LH");
        lv_obj_set_style_text_color(_lbl_lh, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(_lbl_lh, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(_lbl_lh, w / 2 - 130, label_y);

        /* Mag — right side */
        _lbl_source = lv_label_create(_compass);
        lv_label_set_text(_lbl_source, "Mag");
        lv_obj_set_style_text_color(_lbl_source, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(_lbl_source, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(_lbl_source, w / 2 + 110, label_y);

        /* Track — centered, below LH/Mag */
        _lbl_mode = lv_label_create(_compass);
        lv_label_set_text(_lbl_mode, "Track");
        lv_obj_set_style_text_color(_lbl_mode, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(_lbl_mode, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(_lbl_mode, 0, label_y + 18);
        lv_obj_set_width(_lbl_mode, w);
        lv_obj_set_style_text_align(_lbl_mode, LV_TEXT_ALIGN_CENTER, 0);
    }

    /* ═══════════════════════════════════════════════════════════════════════
     * TOUCH BUTTONS (two rows)
     * ═════════════════════════════════════════════════════════════════════ */
    {
        int32_t bw = btn_w;

        int32_t y1 = btn_y0 + btn_gap;
        int32_t y2 = y1 + btn_h + btn_gap;

        auto bx = [&](int col) { return btn_margin + col * (bw + btn_gap); };

        _btn_mode = make_btn(root, "Mode",          bx(0), y1, bw, btn_h, CLR_AP_BTN_BG, CLR_VALUE);
        _btn_m1   = make_btn(root, "-1\xc2\xb0",  bx(1), y1, bw, btn_h, CLR_AP_BTN_BG, CLR_VALUE);
        _btn_p1   = make_btn(root, "+1\xc2\xb0",  bx(2), y1, bw, btn_h, CLR_AP_BTN_BG, CLR_VALUE);
        _btn_menu = make_btn(root, "Menu",         bx(3), y1, bw, btn_h, CLR_AP_BTN_BG, CLR_TITLE);

        _btn_stby = make_btn(root, "STBY",         bx(0), y2, bw, btn_h, CLR_AP_BTN_BG, CLR_AP_STBY);
        _btn_m10  = make_btn(root, "-10\xc2\xb0", bx(1), y2, bw, btn_h, CLR_AP_BTN_BG, CLR_VALUE);
        _btn_p10  = make_btn(root, "+10\xc2\xb0", bx(2), y2, bw, btn_h, CLR_AP_BTN_BG, CLR_VALUE);
        _btn_auto = make_btn(root, "Auto",         bx(3), y2, bw, btn_h, CLR_AP_BTN_BG, CLR_AP_AUTO_GRN);

        /* Register click handlers */
        lv_obj_t *btns[] = { _btn_mode, _btn_m1, _btn_p1, _btn_menu,
                             _btn_stby, _btn_m10, _btn_p10, _btn_auto };
        for (auto *b : btns) {
            lv_obj_add_event_cb(b, on_btn_event, LV_EVENT_CLICKED, this);
            lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        }
    }

    /* Set initial label state + pulse STBY (default standby) */
    update_ap_labels();
    start_pulse(_btn_stby);
}

/* ── Pulse animation for active STBY/Auto button ──────────────────────── */

void AutopilotApp::pulse_anim_cb(void *obj, int32_t val)
{
    AutopilotApp *app = static_cast<AutopilotApp *>(obj);
    app->_pulse_opa = (lv_opa_t)val;

    /* Pulse the active button text */
    lv_obj_t *btn = (app->_ap_engaged == ENGAGED_AUTO) ? app->_btn_auto : app->_btn_stby;
    if (btn) {
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        if (lbl) lv_obj_set_style_text_opa(lbl, (lv_opa_t)val, 0);
    }

    /* Invalidate compass to redraw the set angle marker */
    if (app->_compass && app->_ap_engaged == ENGAGED_AUTO) {
        lv_obj_invalidate(app->_compass);
    }
}

void AutopilotApp::start_pulse(lv_obj_t *btn)
{
    stop_pulse();

    lv_anim_init(&_pulse_anim);
    lv_anim_set_var(&_pulse_anim, this);
    lv_anim_set_exec_cb(&_pulse_anim, pulse_anim_cb);
    lv_anim_set_values(&_pulse_anim, LV_OPA_COVER, LV_OPA_70);
    lv_anim_set_time(&_pulse_anim, 1600);
    lv_anim_set_playback_time(&_pulse_anim, 1600);
    lv_anim_set_repeat_count(&_pulse_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&_pulse_anim);
    _pulse_active = true;
}

void AutopilotApp::stop_pulse()
{
    if (_pulse_active) {
        lv_anim_del(this, pulse_anim_cb);
        /* Restore full text opacity on both buttons */
        lv_obj_t *btns[] = {_btn_stby, _btn_auto};
        for (lv_obj_t *b : btns) {
            if (b) {
                lv_obj_t *lbl = lv_obj_get_child(b, 0);
                if (lbl) lv_obj_set_style_text_opa(lbl, LV_OPA_COVER, 0);
            }
        }
        _pulse_opa = LV_OPA_COVER;
        _pulse_active = false;
    }
}

/* ── Button event handler ──────────────────────────────────────────────── */

void AutopilotApp::on_btn_event(lv_event_t *e)
{
    AutopilotApp *app = static_cast<AutopilotApp *>(lv_event_get_user_data(e));
    lv_obj_t *btn = lv_event_get_current_target(e);

    ESP_LOGI(TAG, "Button pressed: btn=%p mode=%p auto=%p stby=%p m1=%p p1=%p m10=%p p10=%p",
             btn, app->_btn_mode, app->_btn_auto, app->_btn_stby,
             app->_btn_m1, app->_btn_p1, app->_btn_m10, app->_btn_p10);

    // State names matching APMode enum: auto=compass, wind=wind, route=track
    static const char *state_api[] = { "auto", "wind", "route" };

    if (btn == app->_btn_mode) {
        // Mode only changes in standby — ignored while engaged
        if (app->_ap_engaged == ENGAGED_STBY) {
            audio_feedback_click();
            app->_ap_mode = (APMode)((app->_ap_mode + 1) % 3);
            ESP_LOGI(TAG, "Mode → %d", app->_ap_mode);
        } else {
            audio_feedback_invalid();
        }
    }
    else if (btn == app->_btn_auto) {
        if (app->_ap_engaged != ENGAGED_AUTO) {
            audio_feedback_engage();
            /* Get fresh data for locking */
            static InstrumentStore d;
            if (signalk_client_get_data(&d)) {
                const PathValue *hm = store_find(d, "navigation.headingMagnetic");
                const PathValue *ht = store_find(d, "navigation.headingTrue");
                const PathValue *h = (hm && hm->valid) ? hm : ht;
                if (h && h->valid) {
                    app->_locked_heading = fmodf(h->value * 57.2957795f, 360.0f);
                    if (app->_locked_heading < 0) app->_locked_heading += 360.0f;
                }
                const PathValue *wa = store_find(d, "environment.wind.angleApparent");
                if (wa && wa->valid) {
                    app->_locked_wind_angle = wa->value * 57.2957795f;
                }
            }
            app->_ap_engaged = ENGAGED_AUTO;
            app->start_pulse(app->_btn_auto);
            ESP_LOGI(TAG, "Auto engaged: hdg=%.0f wind=%.0f", app->_locked_heading, app->_locked_wind_angle);

            // Engage with state matching current mode
            char body[48];
            snprintf(body, sizeof(body), "{\"value\":\"%s\"}", state_api[app->_ap_mode]);
            char path[128];
            snprintf(path, sizeof(path), "%s/state", AP_BASE);
            signalk_auth_api_call(HTTP_METHOD_PUT, path, body);

            static const char *mode_label[] = { "Compass mode", "Wind mode", "Track mode" };
            app->show_overlay("Autopilot Engaged", mode_label[app->_ap_mode]);
        } else {
            audio_feedback_invalid();  // already engaged
        }
    }
    else if (btn == app->_btn_stby) {
        if (app->_ap_engaged == ENGAGED_AUTO) {
            audio_feedback_disengage();
            app->_ap_engaged = ENGAGED_STBY;
            app->start_pulse(app->_btn_stby);
            ESP_LOGI(TAG, "Standby");

            char path[128];
            snprintf(path, sizeof(path), "%s/state", AP_BASE);
            signalk_auth_api_call(HTTP_METHOD_PUT, path, "{\"value\":\"standby\"}");

            app->show_overlay("Standby", "Autopilot disengaged");
        } else {
            audio_feedback_invalid();  // already standby
        }
    }
    else if (btn == app->_btn_menu) {
        audio_feedback_click();
        app->show_menu();
        return;   /* don't call update_ap_labels */
    }
    else if (app->_ap_engaged == ENGAGED_AUTO) {
        audio_feedback_click();
        /* ±1° / ±10° adjust locked value */
        float delta = 0.0f;
        if      (btn == app->_btn_m1)  delta = -1.0f;
        else if (btn == app->_btn_p1)  delta =  1.0f;
        else if (btn == app->_btn_m10) delta = -10.0f;
        else if (btn == app->_btn_p10) delta =  10.0f;

        if (delta != 0.0f) {
            if (app->_ap_mode == MODE_COMPASS) {
                app->_locked_heading = fmodf(app->_locked_heading + delta + 360.0f, 360.0f);
            } else if (app->_ap_mode == MODE_WIND) {
                app->_locked_wind_angle += delta;
                if (app->_locked_wind_angle > 180.0f)  app->_locked_wind_angle -= 360.0f;
                if (app->_locked_wind_angle < -180.0f) app->_locked_wind_angle += 360.0f;
            }

            char body[64];
            snprintf(body, sizeof(body), "{\"value\":%d,\"units\":\"deg\"}", (int)delta);
            char path[128];
            snprintf(path, sizeof(path), "%s/target/adjust", AP_BASE);
            signalk_auth_api_call(HTTP_METHOD_PUT, path, body);
        }
    }
    else {
        // ±adjust in standby — invalid
        audio_feedback_invalid();
    }

    app->update_ap_labels();
}

void AutopilotApp::update_ap_labels()
{
    if (!_lbl_pilot_mode || !_lbl_wind_val) return;

    /* Left label — mode name */
    static const char *mode_str[] = { "Compass", "Wind", "Track" };
    lv_label_set_text(_lbl_pilot_mode, mode_str[_ap_mode]);

    /* Right label — set locked value immediately on button press (auto mode) */
    if (_ap_engaged == ENGAGED_AUTO) {
        char buf[16];
        switch (_ap_mode) {
            case MODE_COMPASS:
                snprintf(buf, sizeof(buf), "%.0f\xc2\xb0", _locked_heading);
                break;
            case MODE_WIND: {
                int abs_deg = (int)fabsf(_locked_wind_angle);
                snprintf(buf, sizeof(buf), "%d\xc2\xb0%c", abs_deg,
                         _locked_wind_angle >= 0 ? 'S' : 'P');
            }   break;
            case MODE_TRACK:
                /* XTE shown by update_display */
                break;
        }
        if (_ap_mode != MODE_TRACK) {
            lv_label_set_text(_lbl_wind_val, buf);
        }
    }
    /* Standby right label is set by update_display with live data */

    lv_obj_set_style_text_color(_lbl_wind_val, lv_color_hex(0xffffff), 0);
    ESP_LOGI(TAG, "update_ap_labels: mode=%s engaged=%d", mode_str[_ap_mode], _ap_engaged);
}

/* ── Status overlay ────────────────────────────────────────────────────── */

void AutopilotApp::show_overlay(const char *line1, const char *line2)
{
    // Kill previous timer/overlay if still visible
    if (_overlay_timer) {
        lv_timer_del(_overlay_timer);
        _overlay_timer = nullptr;
    }
    if (_overlay) {
        lv_obj_del(_overlay);
        _overlay = nullptr;
    }

    lv_obj_t *screen = lv_scr_act();

    // Semi-transparent full-screen backdrop
    _overlay = lv_obj_create(screen);
    lv_obj_set_size(_overlay, 500, 200);
    lv_obj_align(_overlay, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_radius(_overlay, 20, 0);
    lv_obj_set_style_bg_color(_overlay, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(_overlay, 3, 0);
    lv_obj_set_style_pad_all(_overlay, 0, 0);
    lv_obj_clear_flag(_overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Line 1 — large bold text (e.g. "Autopilot Engaged")
    _overlay_line1 = lv_label_create(_overlay);
    lv_label_set_text(_overlay_line1, line1);
    lv_obj_set_style_text_font(_overlay_line1, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(_overlay_line1, lv_color_hex(0x000000), 0);
    lv_obj_align(_overlay_line1, LV_ALIGN_CENTER, 0, -30);

    // Line 2 — smaller text (e.g. "Compass mode")
    _overlay_line2 = lv_label_create(_overlay);
    lv_label_set_text(_overlay_line2, line2);
    lv_obj_set_style_text_font(_overlay_line2, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_overlay_line2, lv_color_hex(0x333333), 0);
    lv_obj_align(_overlay_line2, LV_ALIGN_CENTER, 0, 30);

    // Start 3s timer to fade and delete
    _overlay_timer = lv_timer_create(overlay_fade_cb, 3000, this);
    lv_timer_set_repeat_count(_overlay_timer, 1);
}

void AutopilotApp::overlay_fade_cb(lv_timer_t *t)
{
    AutopilotApp *app = static_cast<AutopilotApp *>(t->user_data);

    if (app->_overlay) {
        lv_obj_del(app->_overlay);
        app->_overlay = nullptr;
        app->_overlay_line1 = nullptr;
        app->_overlay_line2 = nullptr;
    }
    app->_overlay_timer = nullptr;
}

/* ── Menu page ─────────────────────────────────────────────────────────── */

void AutopilotApp::show_menu()
{
    if (_menu_page) return;

    lv_obj_add_flag(_main_page, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *screen = lv_obj_get_parent(_main_page);
    _menu_page = lv_obj_create(screen);

    lv_area_t vis = getVisualArea();
    int32_t w = vis.x2 - vis.x1 + 1;
    int32_t h = vis.y2 - vis.y1 + 1;

    lv_obj_set_pos(_menu_page, vis.x1, vis.y1);
    lv_obj_set_size(_menu_page, w, h);
    lv_obj_set_style_bg_color(_menu_page, CLR_BG, 0);
    lv_obj_set_style_bg_opa(_menu_page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_menu_page, 0, 0);
    lv_obj_set_style_pad_all(_menu_page, 0, 0);
    lv_obj_clear_flag(_menu_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(_menu_page, 0);

    int32_t cx = w / 2;   /* center x */

    /* ── SignalK Server card ───────────────────────────────────────────── */
    lv_obj_t *card1 = lv_obj_create(_menu_page);
    lv_obj_set_size(card1, w - 40, 160);
    lv_obj_set_pos(card1, 20, 30);
    lv_obj_set_style_bg_color(card1, lv_color_hex(0x1b2838), 0);
    lv_obj_set_style_bg_opa(card1, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card1, lv_color_hex(0x415a77), 0);
    lv_obj_set_style_border_width(card1, 1, 0);
    lv_obj_set_style_radius(card1, 12, 0);
    lv_obj_set_style_pad_all(card1, 20, 0);
    lv_obj_clear_flag(card1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(card1, 0);

    lv_obj_t *lbl_hdr1 = lv_label_create(card1);
    lv_label_set_text(lbl_hdr1, "SignalK Server");
    lv_obj_set_style_text_color(lbl_hdr1, lv_color_hex(0x778da9), 0);
    lv_obj_set_style_text_font(lbl_hdr1, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_hdr1, 0, 0);

    _lbl_server_status = lv_label_create(card1);
    lv_obj_set_style_text_font(_lbl_server_status, &lv_font_montserrat_48, 0);
    lv_obj_set_pos(_lbl_server_status, 0, 20);
    if (_sk_connected) {
        lv_label_set_text(_lbl_server_status, "Connected");
        lv_obj_set_style_text_color(_lbl_server_status, lv_color_hex(0x00cc66), 0);
    } else {
        lv_label_set_text(_lbl_server_status, "Disconnected");
        lv_obj_set_style_text_color(_lbl_server_status, lv_color_hex(0xcc3333), 0);
    }

    _lbl_server_url = lv_label_create(card1);
#ifdef CONFIG_SIGNALK_WS_URI
    lv_label_set_text(_lbl_server_url, CONFIG_SIGNALK_WS_URI);
#else
    lv_label_set_text(_lbl_server_url, "ws://localhost:3000/signalk/v1/stream");
#endif
    lv_obj_set_style_text_color(_lbl_server_url, lv_color_hex(0x778da9), 0);
    lv_obj_set_style_text_font(_lbl_server_url, &lv_font_montserrat_14, 0);
    lv_obj_set_width(_lbl_server_url, w - 80);
    lv_obj_set_pos(_lbl_server_url, 0, 80);

    /* ── Autopilot Control card ────────────────────────────────────────── */
    lv_obj_t *card2 = lv_obj_create(_menu_page);
    lv_obj_set_size(card2, w - 40, 260);
    lv_obj_set_pos(card2, 20, 210);
    lv_obj_set_style_bg_color(card2, lv_color_hex(0x1b2838), 0);
    lv_obj_set_style_bg_opa(card2, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card2, lv_color_hex(0x415a77), 0);
    lv_obj_set_style_border_width(card2, 1, 0);
    lv_obj_set_style_radius(card2, 12, 0);
    lv_obj_set_style_pad_all(card2, 20, 0);
    lv_obj_clear_flag(card2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(card2, 0);

    lv_obj_t *lbl_hdr2 = lv_label_create(card2);
    lv_label_set_text(lbl_hdr2, "Autopilot Control");
    lv_obj_set_style_text_color(lbl_hdr2, lv_color_hex(0x778da9), 0);
    lv_obj_set_style_text_font(lbl_hdr2, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_hdr2, 0, 0);

    _lbl_auth_status = lv_label_create(card2);
    lv_obj_set_style_text_font(_lbl_auth_status, &lv_font_montserrat_48, 0);
    lv_obj_set_pos(_lbl_auth_status, 0, 20);

    _lbl_auth_info = lv_label_create(card2);
    lv_obj_set_style_text_font(_lbl_auth_info, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_lbl_auth_info, lv_color_hex(0x778da9), 0);
    lv_obj_set_width(_lbl_auth_info, w - 80);
    lv_obj_set_pos(_lbl_auth_info, 0, 80);

    int32_t card2_w = w - 80;   /* inner width of card2 */
    _btn_pair = make_btn(card2, "Pair Device", card2_w / 2 - 130, 130, 260, 80,
                          lv_color_hex(0x00cc66), lv_color_hex(0xffffff));
    lv_obj_add_event_cb(_btn_pair, on_pair_btn, LV_EVENT_CLICKED, this);
    lv_obj_add_flag(_btn_pair, LV_OBJ_FLAG_CLICKABLE);

    // Sync auth state from module
    signalk_auth_state_t auth_now = signalk_auth_get_state();
    if (auth_now == SK_AUTH_APPROVED) _auth_state = AUTH_AUTHORIZED;
    else if (auth_now == SK_AUTH_NONE) _auth_state = AUTH_NONE;

    switch (_auth_state) {
        case AUTH_NONE:
            lv_label_set_text(_lbl_auth_status, "Not Authorized");
            lv_obj_set_style_text_color(_lbl_auth_status, lv_color_hex(0xe74c3c), 0);
            lv_label_set_text(_lbl_auth_info, "Pair this device to enable\nautopilot control");
            break;
        case AUTH_REQUESTING:
            lv_label_set_text(_lbl_auth_status, "Requesting...");
            lv_obj_set_style_text_color(_lbl_auth_status, lv_color_hex(0xf39c12), 0);
            lv_label_set_text(_lbl_auth_info, "Approve on SignalK web UI");
            lv_obj_add_flag(_btn_pair, LV_OBJ_FLAG_HIDDEN);
            break;
        case AUTH_AUTHORIZED:
            lv_label_set_text(_lbl_auth_status, "Authorized");
            lv_obj_set_style_text_color(_lbl_auth_status, lv_color_hex(0x00cc66), 0);
            lv_label_set_text(_lbl_auth_info, "Autopilot control enabled");
            lv_obj_add_flag(_btn_pair, LV_OBJ_FLAG_HIDDEN);
            break;
    }

    /* ── Back button — bottom left ─────────────────────────────────────── */
    lv_obj_t *back = make_btn(_menu_page, "Back", 20, h - 110, 150, 80,
                               CLR_AP_BTN_BG, lv_color_hex(0xffffff));
    lv_obj_add_event_cb(back, on_menu_back, LV_EVENT_CLICKED, this);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);

    ESP_LOGI(TAG, "Menu opened, auth_state=%d", _auth_state);
}

void AutopilotApp::hide_menu()
{
    if (!_menu_page) return;

    lv_obj_del(_menu_page);
    _menu_page = nullptr;

    /* Show main page again */
    lv_obj_clear_flag(_main_page, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Menu closed");
}

void AutopilotApp::on_pair_btn(lv_event_t *e)
{
    AutopilotApp *app = static_cast<AutopilotApp *>(lv_event_get_user_data(e));
    ESP_LOGI(TAG, "Pair button pressed");

    signalk_auth_request_access();
    app->_auth_state = AUTH_REQUESTING;

    /* Update UI */
    if (app->_lbl_auth_status) {
        lv_label_set_text(app->_lbl_auth_status, "Requesting...");
        lv_obj_set_style_text_color(app->_lbl_auth_status, lv_color_hex(0xf39c12), 0);
    }
    if (app->_lbl_auth_info) {
        lv_label_set_text(app->_lbl_auth_info, "Approve on SignalK web UI");
    }
    if (app->_btn_pair) {
        lv_obj_add_flag(app->_btn_pair, LV_OBJ_FLAG_HIDDEN);
    }
}

void AutopilotApp::on_menu_back(lv_event_t *e)
{
    AutopilotApp *app = static_cast<AutopilotApp *>(lv_event_get_user_data(e));
    app->hide_menu();
}

/* ── Display update ────────────────────────────────────────────────────── */

void AutopilotApp::on_update_timer(lv_timer_t *t)
{
    static_cast<AutopilotApp *>(t->user_data)->update_display();
}

void AutopilotApp::update_display()
{
    /* Guard against being called before build_ui completes */
    if (!_compass || !_lbl_heading || !_lbl_wind_val || !_wind_indicator) return;

    bool was_connected = _sk_connected;
    _sk_connected = signalk_client_is_connected();
    if (_sk_connected != was_connected && _wind_bar) {
        lv_obj_invalidate(_wind_bar);
    }

    static InstrumentStore d;
    if (!signalk_client_get_data(&d)) return;

    char buf[16];

    /* Heading — prefer magnetic, fall back to true */
    const PathValue *hm = store_find(d, "navigation.headingMagnetic");
    const PathValue *ht = store_find(d, "navigation.headingTrue");
    const PathValue *hdg = (hm && hm->valid) ? hm : ht;
    if (hdg && !instrument_is_stale(*hdg)) {
        float deg = fmodf(hdg->value * 57.2957795f, 360.0f);
        if (deg < 0) deg += 360.0f;

        snprintf(buf, sizeof(buf), "%03.0f\xc2\xb0", deg);
        lv_label_set_text(_lbl_heading, buf);
        lv_obj_set_style_text_color(_lbl_heading, lv_color_hex(0xffffff), 0);  /* WHITE */

        _heading_deg = deg;
        lv_obj_invalidate(_compass);

        lv_label_set_text(_lbl_source, (hm && hm->valid) ? "Mag" : "True");
    } else {
        lv_label_set_text(_lbl_heading, "---\xc2\xb0");
        lv_obj_set_style_text_color(_lbl_heading, CLR_STALE, 0);
    }

    /* Rudder angle bar — filled from center */
    const PathValue *rud = store_find(d, "steering.rudderAngle");
    if (rud && !instrument_is_stale(*rud)) {
        float rud_deg = rud->value * 57.2957795f;
        float clamped = rud_deg;
        if (clamped >  30.0f) clamped =  30.0f;
        if (clamped < -30.0f) clamped = -30.0f;

        int32_t bar_w = (int32_t)(fabsf(clamped) / 30.0f * _wind_bar_hw);
        int32_t bar_x = (clamped >= 0) ? _wind_bar_cx : _wind_bar_cx - bar_w;
        lv_obj_set_pos(_wind_indicator, bar_x, lv_obj_get_y(_wind_indicator));
        lv_obj_set_width(_wind_indicator, bar_w);

        /* Green for starboard, red for port */
        lv_obj_set_style_bg_color(_wind_indicator,
                                   rud_deg >= 0 ? lv_color_hex(0x0db134) : lv_color_hex(0xe60203), 0);
    } else {
        /* No rudder data — center the bar (zero deflection) */
        lv_obj_set_pos(_wind_indicator, _wind_bar_cx, lv_obj_get_y(_wind_indicator));
        lv_obj_set_width(_wind_indicator, 0);
    }

    /* Wind angle — always update arc indicator */
    const PathValue *wa = store_find(d, "environment.wind.angleApparent");
    if (wa && !instrument_is_stale(*wa)) {
        _wind_angle_deg = wa->value * 57.2957795f;
        _wind_angle_valid = true;
    } else {
        if (_wind_angle_valid) ESP_LOGW(TAG, "Wind angle went stale");
        _wind_angle_valid = false;
    }

    /* ── Right label — live data in standby, locked in auto ────────────── */
    bool connected = _sk_connected;

    if (!connected) {
        /* No SignalK connection */
        lv_label_set_text(_lbl_wind_val, "--");
        lv_obj_set_style_text_color(_lbl_wind_val, CLR_STALE, 0);
    } else if (_ap_engaged == ENGAGED_STBY) {
        /* Standby — show live data matching current mode */
        switch (_ap_mode) {
            case MODE_COMPASS:
                if (hdg && !instrument_is_stale(*hdg)) {
                    snprintf(buf, sizeof(buf), "%.0f\xc2\xb0", _heading_deg);
                    lv_label_set_text(_lbl_wind_val, buf);
                    lv_obj_set_style_text_color(_lbl_wind_val, lv_color_hex(0xffffff), 0);
                } else {
                    lv_label_set_text(_lbl_wind_val, "--");
                    lv_obj_set_style_text_color(_lbl_wind_val, CLR_STALE, 0);
                }
                break;
            case MODE_WIND:
                if (_wind_angle_valid) {
                    int abs_deg = (int)fabsf(_wind_angle_deg);
                    snprintf(buf, sizeof(buf), "%d\xc2\xb0%c", abs_deg,
                             _wind_angle_deg >= 0 ? 'S' : 'P');
                    lv_label_set_text(_lbl_wind_val, buf);
                    lv_obj_set_style_text_color(_lbl_wind_val, lv_color_hex(0xffffff), 0);
                } else {
                    lv_label_set_text(_lbl_wind_val, "--");
                    lv_obj_set_style_text_color(_lbl_wind_val, CLR_STALE, 0);
                }
                break;
            case MODE_TRACK: {
                const PathValue *xte = store_find(d, "navigation.courseRhumbline.crossTrackError");
                if (xte && !instrument_is_stale(*xte)) {
                    float xte_nm = unit_convert(UNIT_CAT_DISTANCE, xte->value);
                    snprintf(buf, sizeof(buf), "%.2f%c", fabsf(xte_nm),
                             xte_nm >= 0 ? 'R' : 'L');
                    lv_label_set_text(_lbl_wind_val, buf);
                    lv_obj_set_style_text_color(_lbl_wind_val, lv_color_hex(0xffffff), 0);
                } else {
                    lv_label_set_text(_lbl_wind_val, "--");
                    lv_obj_set_style_text_color(_lbl_wind_val, CLR_STALE, 0);
                }
                break;
            }
        }
    } else {
        /* Auto — locked values (set by update_ap_labels on button press) */
        /* Track mode still shows live XTE */
        if (_ap_mode == MODE_TRACK) {
            const PathValue *xte = store_find(d, "navigation.courseRhumbline.crossTrackError");
            if (xte && !instrument_is_stale(*xte)) {
                float xte_nm = unit_convert(UNIT_CAT_DISTANCE, xte->value);
                snprintf(buf, sizeof(buf), "%.2f%c", fabsf(xte_nm),
                         xte_nm >= 0 ? 'R' : 'L');
                lv_label_set_text(_lbl_wind_val, buf);
                lv_obj_set_style_text_color(_lbl_wind_val, lv_color_hex(0xffffff), 0);
            } else {
                lv_label_set_text(_lbl_wind_val, "--");
                lv_obj_set_style_text_color(_lbl_wind_val, CLR_STALE, 0);
            }
        }
        /* Compass/Wind auto labels managed by update_ap_labels() */
    }

    /* ── Sync auth state from signalk_auth module ────────────────────────── */
    signalk_auth_state_t auth = signalk_auth_get_state();
    if (auth == SK_AUTH_APPROVED && _auth_state != AUTH_AUTHORIZED) {
        _auth_state = AUTH_AUTHORIZED;
        if (_lbl_auth_status) {
            lv_label_set_text(_lbl_auth_status, "Authorized");
            lv_obj_set_style_text_color(_lbl_auth_status, lv_color_hex(0x00cc66), 0);
        }
        if (_lbl_auth_info) lv_label_set_text(_lbl_auth_info, "Autopilot control enabled");
        if (_btn_pair) lv_obj_add_flag(_btn_pair, LV_OBJ_FLAG_HIDDEN);
    } else if ((auth == SK_AUTH_ERROR || auth == SK_AUTH_DENIED) && _auth_state == AUTH_REQUESTING) {
        _auth_state = AUTH_NONE;
        const char *err_msg = signalk_auth_get_error();
        if (_lbl_auth_status) {
            lv_label_set_text(_lbl_auth_status, auth == SK_AUTH_DENIED ? "Denied" : "Error");
            lv_obj_set_style_text_color(_lbl_auth_status, lv_color_hex(0xe74c3c), 0);
        }
        if (_lbl_auth_info) {
            lv_label_set_text(_lbl_auth_info, err_msg ? err_msg : "Unknown error — try again");
        }
        if (_btn_pair) lv_obj_clear_flag(_btn_pair, LV_OBJ_FLAG_HIDDEN);
    }

    /* Debug: log label state every 5 seconds */
    static uint32_t last_dbg = 0;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    if (now - last_dbg >= 5) {
        last_dbg = now;
        ESP_LOGI(TAG, "DIAG: mode=%d engaged=%d left='%s' right='%s'",
                 _ap_mode, _ap_engaged,
                 _lbl_pilot_mode ? lv_label_get_text(_lbl_pilot_mode) : "NULL",
                 _lbl_wind_val ? lv_label_get_text(_lbl_wind_val) : "NULL");
    }
}
