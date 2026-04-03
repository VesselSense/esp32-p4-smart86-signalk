/*
 * LVGL headless simulator — renders the instrument panel to PNG files.
 *
 * Compiles signalk_app.cpp directly against stub implementations of:
 *   - ESP_UI_PhoneApp  (esp_ui.hpp stub)
 *   - signalk_client   (signalk_client_stub.cpp with hardcoded sample data)
 *   - esp_log / esp_timer / sdkconfig / freertos (minimal stubs)
 *
 * Usage:
 *   ./lvgl_sim        — renders all 4 pages → screenshots/page{1..4}.png
 *   ./lvgl_sim 3      — renders page 3 only → screenshots/page3.png
 *
 * Output goes to the current working directory. render.sh cd's to
 * simulator/screenshots/ before invoking this binary.
 */

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "lvgl.h"
#include "autopilot_app.hpp"
#include "wind_rose_app.hpp"
#include "wifi_settings_app.hpp"
#include "settings_app.hpp"
#include "data_browser_app.hpp"
#include "signalk_client.hpp"
#include "sk_logo_alpha.h"
#include "helm_icon_alpha.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Display dimensions — match the real Waveshare 720x720 panel
 * ---------------------------------------------------------------------- */
#define DISPLAY_W 720
#define DISPLAY_H 720

/* RGBA framebuffer written by flush_cb, saved as PNG */
static uint8_t fb_rgba[DISPLAY_W * DISPLAY_H * 4];

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px)
{
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            int idx = (y * DISPLAY_W + x) * 4;
            fb_rgba[idx + 0] = px->ch.red;
            fb_rgba[idx + 1] = px->ch.green;
            fb_rgba[idx + 2] = px->ch.blue;
            fb_rgba[idx + 3] = 0xFF;
            px++;
        }
    }
    lv_disp_flush_ready(drv);
}

/* -------------------------------------------------------------------------
 * SimulatorApp — thin subclasses to call the protected run() from main()
 * ---------------------------------------------------------------------- */
class SimulatorAutopilotApp : public AutopilotApp {
public:
    bool start() { return run(); }
};

class SimulatorWindRoseApp : public WindRoseApp {
public:
    bool start() { return run(); }
};

class SimulatorSettingsApp : public SettingsApp {
public:
    bool start() { return run(); }
    void openPicker(unit_category_t cat) { show_picker(cat); }
};

class SimulatorDataBrowserApp : public DataBrowserApp {
public:
    bool start() { return run(); }
    void openDetail(int idx) {
        show_detail_view(idx);
        // Manually set a sample value for screenshot (observer won't fire in sim)
        if (_detail_value && _detail_unit && idx < _path_count) {
            const char *units = _paths[idx].units;
            // Pick representative values based on unit type
            if (strcmp(units, "m/s") == 0) {
                lv_label_set_text(_detail_value, "6.2");
            } else if (strcmp(units, "rad") == 0) {
                lv_label_set_text(_detail_value, "215");
            } else if (strcmp(units, "K") == 0) {
                lv_label_set_text(_detail_value, "18.8");
            } else if (strcmp(units, "m") == 0) {
                lv_label_set_text(_detail_value, "49.9");
            } else if (strcmp(units, "V") == 0) {
                lv_label_set_text(_detail_value, "14.54");
            } else if (strcmp(units, "ratio") == 0) {
                lv_label_set_text(_detail_value, "66");
            } else {
                lv_label_set_text(_detail_value, "42.7");
            }
            lv_obj_set_style_text_color(_detail_value, lv_color_white(), 0);

            // Dynamic font size based on character count
            LV_FONT_DECLARE(lv_font_montserrat_120);
            LV_FONT_DECLARE(lv_font_montserrat_310);
            LV_FONT_DECLARE(lv_font_montserrat_360);
            const char *val_text = lv_label_get_text(_detail_value);
            int val_len = (int)strlen(val_text);
            const lv_font_t *vf;
            if (val_len <= 2)      vf = &lv_font_montserrat_360;
            else if (val_len <= 3) vf = &lv_font_montserrat_310;
            else                   vf = &lv_font_montserrat_120;
            lv_obj_set_style_text_font(_detail_value, vf, 0);

            lv_obj_align(_detail_value, LV_ALIGN_CENTER, 0, 0);
            lv_obj_update_layout(_detail_value);

            // Position unit using the same logic as on_value_changed
            char unit_buf[16] = {};
            if (strcmp(units, "m/s") == 0) snprintf(unit_buf, sizeof(unit_buf), "kts");
            else if (strcmp(units, "rad") == 0) snprintf(unit_buf, sizeof(unit_buf), "°");
            else if (strcmp(units, "K") == 0) snprintf(unit_buf, sizeof(unit_buf), "°C");
            else if (strcmp(units, "m") == 0) snprintf(unit_buf, sizeof(unit_buf), "ft");
            else if (strcmp(units, "V") == 0) snprintf(unit_buf, sizeof(unit_buf), "V");
            else if (strcmp(units, "ratio") == 0) snprintf(unit_buf, sizeof(unit_buf), "%%");
            else if (units[0]) snprintf(unit_buf, sizeof(unit_buf), "%s", units);

            lv_label_set_text(_detail_unit, unit_buf);
            lv_obj_align(_detail_value, LV_ALIGN_CENTER, 0, 0);
            lv_obj_update_layout(_detail_value);

            if (unit_buf[0]) {
                LV_FONT_DECLARE(lv_font_montserrat_90);
                LV_FONT_DECLARE(lv_font_montserrat_120);
                bool is_degree = (strcmp(unit_buf, "°") == 0);
                const lv_font_t *uf = &lv_font_montserrat_48;
                if (is_degree) {
                    if (vf == &lv_font_montserrat_360)      uf = &lv_font_montserrat_120;
                    else if (vf == &lv_font_montserrat_310)  uf = &lv_font_montserrat_90;
                    else                                     uf = &lv_font_montserrat_48;
                }
                lv_obj_set_style_text_font(_detail_unit, uf, 0);
                lv_obj_update_layout(_detail_value);
                lv_obj_update_layout(_detail_unit);
                lv_coord_t vx = lv_obj_get_x(_detail_value);
                lv_coord_t vy = lv_obj_get_y(_detail_value);
                lv_coord_t vw = lv_obj_get_width(_detail_value);
                lv_coord_t vh = lv_obj_get_height(_detail_value);
                bool is_superscript = is_degree ||
                                      strcmp(unit_buf, "%") == 0 ||
                                      strcmp(unit_buf, "°C") == 0;
                if (is_degree) {
                    lv_obj_set_pos(_detail_unit, vx + vw - 10, vy - 30);
                } else if (is_superscript) {
                    lv_obj_set_pos(_detail_unit, vx + vw + 2, vy);
                } else {
                    lv_obj_set_pos(_detail_unit, vx + vw + 4, vy + vh - 52);
                }
            }
        }
    }
};

class SimulatorWifiApp : public WifiSettingsApp {
public:
    bool start() { return run(); }
    void simulateType(const char *text) {
        if (_ssid_ta) {
            lv_textarea_set_text(_ssid_ta, text);
            update_autocomplete();
        }
    }
};

/* -------------------------------------------------------------------------
 * save_png — encode framebuffer to pageN.png in current directory
 * ---------------------------------------------------------------------- */
static bool save_png(const char *name)
{
    char filename[64];
    snprintf(filename, sizeof(filename), "%s.png", name);
    if (stbi_write_png(filename, DISPLAY_W, DISPLAY_H, 4, fb_rgba, DISPLAY_W * 4)) {
        printf("Saved %s (%dx%d)\n", filename, DISPLAY_W, DISPLAY_H);
        return true;
    }
    fprintf(stderr, "Failed to write %s\n", filename);
    return false;
}

/* -------------------------------------------------------------------------
 * Launcher status overlay — renders connection state on a dark background
 * simulating the esp-ui launcher home screen.
 * ---------------------------------------------------------------------- */
static const lv_color_t CLR_BG        = LV_COLOR_MAKE(0x1a, 0x1a, 0x2e);
static const lv_color_t CLR_GREEN     = LV_COLOR_MAKE(0x00, 0xc8, 0x53);
static const lv_color_t CLR_RED       = LV_COLOR_MAKE(0xe0, 0x40, 0x40);
static const lv_color_t CLR_AMBER     = LV_COLOR_MAKE(0xff, 0xa0, 0x40);
static const lv_color_t CLR_LABEL     = LV_COLOR_MAKE(0xcc, 0xcc, 0xcc);

/* Draw the SignalK logo from a pre-rendered alpha map (120×85, 8-bit per pixel).
 * The alpha map was generated from the official 720×720 SignalK PNG via LANCZOS
 * downscale, so edges are already perfectly anti-aliased. We tint it any color
 * by blending alpha against the background. */
static lv_color_t logo_canvas_buf[SK_LOGO_WIDTH * SK_LOGO_HEIGHT];

static inline lv_color_t blend_color(lv_color_t bg, lv_color_t fg, uint8_t mix)
{
    uint8_t r = (bg.ch.red   * (255 - mix) + fg.ch.red   * mix) / 255;
    uint8_t g = (bg.ch.green * (255 - mix) + fg.ch.green * mix) / 255;
    uint8_t b = (bg.ch.blue  * (255 - mix) + fg.ch.blue  * mix) / 255;
    return lv_color_make(r, g, b);
}

static lv_obj_t *create_sk_logo(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, logo_canvas_buf, SK_LOGO_WIDTH, SK_LOGO_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(canvas, CLR_BG, LV_OPA_COVER);

    for (int y = 0; y < SK_LOGO_HEIGHT; y++) {
        for (int x = 0; x < SK_LOGO_WIDTH; x++) {
            uint8_t a = sk_logo_alpha[y][x];
            if (a == 0) continue;
            lv_canvas_set_px_color(canvas, x, y, blend_color(CLR_BG, color, a));
        }
    }
    return canvas;
}

static void render_launcher_state(signalk_conn_state_t state,
                                   const char *status_text,
                                   lv_color_t icon_color)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* --- SignalK logo centred horizontally, upper portion --- */
    lv_obj_t *logo = create_sk_logo(scr, icon_color);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -80);

    /* --- Status text below logo --- */
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, status_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl, CLR_LABEL, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl, 600);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 20);
}

static int render_launcher_states(void)
{
    struct {
        signalk_conn_state_t state;
        const char *text;
        lv_color_t color;
        const char *filename;
    } states[] = {
        { SK_STATE_WIFI_CONNECTING, "Connecting WiFi...",          CLR_AMBER, "launcher_wifi" },
        { SK_STATE_MDNS_SEARCHING,  "Searching for SignalK...",    CLR_AMBER, "launcher_mdns" },
        { SK_STATE_WS_CONNECTING,   "Connecting to server...",       CLR_AMBER, "launcher_ws" },
        { SK_STATE_CONNECTED,       "Connected",                   CLR_GREEN, "launcher_connected" },
        { SK_STATE_DISCONNECTED,    "Disconnected",                CLR_RED,   "launcher_disconnected" },
    };

    for (int i = 0; i < 5; i++) {
        render_launcher_state(states[i].state, states[i].text, states[i].color);

        for (int t = 0; t < 10; t++) {
            lv_tick_inc(10);
            lv_timer_handler();
        }

        if (!save_png(states[i].filename)) return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Icon preview renderer — draws 4 app icons using LVGL primitives
 * Each icon is rendered 300×300 centred on 720×720 for easy review.
 * Final production icons will be 112×112.
 * ---------------------------------------------------------------------- */
#define ICON_SIZE 300
#define ICON_X ((DISPLAY_W - ICON_SIZE) / 2)
#define ICON_Y ((DISPLAY_H - ICON_SIZE) / 2)

static const lv_color_t ICON_BG = LV_COLOR_MAKE(0x1a, 0x1a, 0x2e);  /* launcher bg */

static void icon_clear_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, ICON_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

/* --- SignalK icon: SK logo from alpha map, white tint --- */

/* --- Autopilot icon: ship's wheel (circle + spokes) --- */
static void render_icon_autopilot(void)
{
    icon_clear_screen();
    lv_obj_t *scr = lv_scr_act();
    int cx = DISPLAY_W / 2, cy = DISPLAY_H / 2;
    int r_outer = ICON_SIZE / 2 - 20;
    int r_inner = r_outer - 30;

    /* Outer ring */
    lv_obj_t *arc_o = lv_arc_create(scr);
    lv_obj_set_size(arc_o, r_outer * 2, r_outer * 2);
    lv_arc_set_bg_angles(arc_o, 0, 360);
    lv_arc_set_value(arc_o, 0);
    lv_obj_set_style_arc_width(arc_o, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_o, LV_COLOR_MAKE(0xff, 0xa0, 0x40), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_o, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc_o, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_align(arc_o, LV_ALIGN_CENTER, 0, 0);

    /* Inner ring */
    lv_obj_t *arc_i = lv_arc_create(scr);
    lv_obj_set_size(arc_i, r_inner * 2, r_inner * 2);
    lv_arc_set_bg_angles(arc_i, 0, 360);
    lv_arc_set_value(arc_i, 0);
    lv_obj_set_style_arc_width(arc_i, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_i, LV_COLOR_MAKE(0xff, 0xa0, 0x40), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_i, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc_i, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_align(arc_i, LV_ALIGN_CENTER, 0, 0);

    /* 8 spokes */
    static lv_point_t spokes[8][2];
    for (int i = 0; i < 8; i++) {
        float a = i * 45.0f * 3.14159f / 180.0f;
        spokes[i][0] = {(lv_coord_t)(cx + (int)(r_inner * cosf(a))),
                        (lv_coord_t)(cy + (int)(r_inner * sinf(a)))};
        spokes[i][1] = {(lv_coord_t)(cx + (int)(r_outer * cosf(a))),
                        (lv_coord_t)(cy + (int)(r_outer * sinf(a)))};
        lv_obj_t *line = lv_line_create(scr);
        lv_line_set_points(line, spokes[i], 2);
        lv_obj_set_style_line_color(line, LV_COLOR_MAKE(0xff, 0xa0, 0x40), 0);
        lv_obj_set_style_line_width(line, 5, 0);
        lv_obj_set_style_line_rounded(line, true, 0);
    }

    /* Centre hub */
    lv_obj_t *hub = lv_obj_create(scr);
    lv_obj_set_size(hub, 30, 30);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hub, LV_COLOR_MAKE(0xff, 0xa0, 0x40), 0);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hub, 0, 0);
    lv_obj_align(hub, LV_ALIGN_CENTER, 0, 0);

    /* Label */
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "Pilot");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, LV_COLOR_MAKE(0xff, 0xa0, 0x40), 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, r_outer + 30);
}

/* --- Wind Rose icon: 8-point compass star --- */
static void render_icon_wind_rose(void)
{
    icon_clear_screen();
    lv_obj_t *scr = lv_scr_act();
    int cx = DISPLAY_W / 2, cy = DISPLAY_H / 2;
    int r_outer = ICON_SIZE / 2 - 20;
    int r_inner = r_outer / 3;

    /* Outer circle */
    lv_obj_t *arc = lv_arc_create(scr);
    lv_obj_set_size(arc, r_outer * 2 + 10, r_outer * 2 + 10);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_arc_width(arc, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);

    /* 8-point star: alternate long (cardinal) and short (intercardinal) */
    lv_color_t star_color = lv_color_white();
    static lv_point_t star_lines[8][2];
    for (int i = 0; i < 8; i++) {
        float a = i * 45.0f * 3.14159f / 180.0f - 3.14159f / 2.0f;  /* start from top */
        int r = (i % 2 == 0) ? r_outer : (r_outer * 2 / 3);

        star_lines[i][0] = {(lv_coord_t)cx, (lv_coord_t)cy};
        star_lines[i][1] = {(lv_coord_t)(cx + (int)(r * cosf(a))),
                            (lv_coord_t)(cy + (int)(r * sinf(a)))};
        lv_obj_t *line = lv_line_create(scr);
        lv_line_set_points(line, star_lines[i], 2);
        /* North ray is red, others white */
        lv_color_t c = star_color;
        if (i == 0) c = LV_COLOR_MAKE(0xe0, 0x40, 0x40);
        lv_obj_set_style_line_color(line, c, 0);
        lv_obj_set_style_line_width(line, (i % 2 == 0) ? 4 : 2, 0);
        lv_obj_set_style_line_rounded(line, true, 0);
    }

    /* N marker */
    lv_obj_t *n = lv_label_create(scr);
    lv_label_set_text(n, "N");
    lv_obj_set_style_text_font(n, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(n, LV_COLOR_MAKE(0xe0, 0x40, 0x40), 0);
    lv_obj_align(n, LV_ALIGN_CENTER, 0, -(r_outer + 20));

    /* Label */
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "Rose");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, r_outer + 30);
}

/* -------------------------------------------------------------------------
 * Launcher grid renderer — shows actual lv_img icons as esp-ui displays them.
 * Icons are 112×112 TRUE_COLOR_ALPHA, displayed in a 128×128 container at
 * 70% zoom (~90px), with app name below. Mimics the real esp-ui home screen.
 * ---------------------------------------------------------------------- */
LV_IMG_DECLARE(settings_gear_icon);
LV_IMG_DECLARE(helm_icon);
LV_IMG_DECLARE(wifi_icon);

static int render_launcher_grid(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, LV_COLOR_MAKE(0x1a, 0x1a, 0x2e), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* App definitions: icon + name */
    struct {
        const lv_img_dsc_t *icon;
        const char *name;
    } apps[] = {
        { &settings_gear_icon,                       "Settings" },
        { &helm_icon,                                "Autopilot" },
        { &esp_ui_phone_app_launcher_image_default, "Wind Rose" },
        { &wifi_icon,                                "WiFi" },
    };
    int num_apps = 4;

    /* esp-ui layout: 128×128 container per icon, row-wrap flex, centred */
    int cols = 4;
    int container_size = 128;
    int total_w = cols * container_size;
    int x_start = (DISPLAY_W - total_w) / 2;
    int y_start = 200;  /* upper area of screen */

    /* Zoom: 70% of container = ~90px displayed from 112px source */
    /* LV_IMG_ZOOM_NONE = 256, so 70% = 179 */
    uint16_t zoom = (uint16_t)(0.70f * 256);

    for (int i = 0; i < num_apps; i++) {
        int col = i % cols;
        int row = i / cols;
        int cx = x_start + col * container_size + container_size / 2;
        int cy = y_start + row * (container_size + 40) + container_size / 2;

        /* Icon container — transparent, matches esp-ui */
        lv_obj_t *cont = lv_obj_create(scr);
        lv_obj_set_size(cont, container_size, container_size);
        lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cont, 0, 0);
        lv_obj_set_style_pad_all(cont, 0, 0);
        lv_obj_align(cont, LV_ALIGN_TOP_LEFT,
                     cx - container_size / 2, cy - container_size / 2);

        /* Icon image — zoomed like esp-ui does */
        lv_obj_t *img = lv_img_create(cont);
        lv_img_set_src(img, apps[i].icon);
        lv_img_set_zoom(img, zoom);
        lv_obj_center(img);

        /* App name label below */
        lv_obj_t *lbl = lv_label_create(scr);
        lv_label_set_text(lbl, apps[i].name);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(lbl, container_size);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT,
                     cx - container_size / 2, cy + container_size / 2 + 4);
    }

    for (int t = 0; t < 10; t++) {
        lv_tick_inc(10);
        lv_timer_handler();
    }

    return save_png("launcher_grid") ? 0 : 1;
}

static int render_icons(void)
{
    struct {
        void (*render)(void);
        const char *name;
    } icons[] = {
        { render_icon_autopilot,  "icon_autopilot" },
        { render_icon_wind_rose,  "icon_wind_rose" },
    };

    for (int i = 0; i < 2; i++) {
        icons[i].render();
        for (int t = 0; t < 10; t++) {
            lv_tick_inc(10);
            lv_timer_handler();
        }
        if (!save_png(icons[i].name)) return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Keyboard preview — renders lv_keyboard + lv_textarea on a WiFi settings
 * screen to show what the built-in LVGL keyboard looks like at 720×720.
 * ---------------------------------------------------------------------- */
static int render_keyboard(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, LV_COLOR_MAKE(0x1a, 0x1a, 0x2e), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "WiFi Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    /* SSID label + textarea */
    lv_obj_t *ssid_lbl = lv_label_create(scr);
    lv_label_set_text(ssid_lbl, "Network:");
    lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ssid_lbl, lv_color_white(), 0);
    lv_obj_align(ssid_lbl, LV_ALIGN_TOP_LEFT, 20, 55);

    lv_obj_t *ssid_ta = lv_textarea_create(scr);
    lv_textarea_set_one_line(ssid_ta, true);
    lv_textarea_set_placeholder_text(ssid_ta, "Enter SSID");
    lv_obj_set_width(ssid_ta, 540);
    lv_obj_align(ssid_ta, LV_ALIGN_TOP_LEFT, 160, 50);

    /* Password label + textarea */
    lv_obj_t *pw_lbl = lv_label_create(scr);
    lv_label_set_text(pw_lbl, "Password:");
    lv_obj_set_style_text_font(pw_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(pw_lbl, lv_color_white(), 0);
    lv_obj_align(pw_lbl, LV_ALIGN_TOP_LEFT, 20, 105);

    lv_obj_t *pw_ta = lv_textarea_create(scr);
    lv_textarea_set_one_line(pw_ta, true);
    lv_textarea_set_placeholder_text(pw_ta, "Enter password");
    lv_textarea_set_password_mode(pw_ta, true);
    lv_obj_set_width(pw_ta, 540);
    lv_obj_align(pw_ta, LV_ALIGN_TOP_LEFT, 160, 100);

    /* Keyboard — takes bottom portion of screen */
    lv_obj_t *kb = lv_keyboard_create(scr);
    lv_keyboard_set_textarea(kb, ssid_ta);
    lv_obj_set_size(kb, 720, 380);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);

    for (int t = 0; t < 10; t++) {
        lv_tick_inc(10);
        lv_timer_handler();
    }

    return save_png("keyboard") ? 0 : 1;
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    /* Initialise LVGL */
    lv_init();

    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf[DISPLAY_W * DISPLAY_H];
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, DISPLAY_W * DISPLAY_H);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = DISPLAY_W;
    disp_drv.ver_res      = DISPLAY_H;
    disp_drv.flush_cb     = flush_cb;
    disp_drv.draw_buf     = &draw_buf;
    disp_drv.full_refresh = 1;
    lv_disp_drv_register(&disp_drv);

    /* Check if rendering the autopilot app */
    bool render_autopilot      = (argc > 1 && strcmp(argv[1], "autopilot") == 0);
    bool render_autopilot_menu = (argc > 1 && strcmp(argv[1], "autopilot-menu") == 0);
    bool render_windrose       = (argc > 1 && strcmp(argv[1], "windrose") == 0);
    bool render_launcher       = (argc > 1 && strcmp(argv[1], "launcher") == 0);
    bool render_icons_mode     = (argc > 1 && strcmp(argv[1], "icons") == 0);
    bool render_grid           = (argc > 1 && strcmp(argv[1], "launcher-grid") == 0);
    bool render_kb             = (argc > 1 && strcmp(argv[1], "keyboard") == 0);
    bool render_wifi           = (argc > 1 && strcmp(argv[1], "wifi") == 0);
    bool render_settings       = (argc > 1 && strcmp(argv[1], "settings") == 0);
    bool render_browser        = (argc > 1 && strcmp(argv[1], "browser") == 0);

    if (render_browser) {
        SimulatorDataBrowserApp browser;
        browser.start();
        for (int t = 0; t < 20; t++) { lv_tick_inc(10); lv_timer_handler(); }

        // Sub-mode: "browser detail" renders a detail page
        if (argc > 2 && strcmp(argv[2], "detail") == 0) {
            int idx = (argc > 3) ? atoi(argv[3]) : 0;
            browser.openDetail(idx);
            for (int t = 0; t < 10; t++) { lv_tick_inc(10); lv_timer_handler(); }
            return save_png("browser_detail") ? 0 : 1;
        }
        return save_png("browser") ? 0 : 1;
    } else if (render_settings) {
        SimulatorSettingsApp settings;
        settings.start();
        for (int t = 0; t < 10; t++) { lv_tick_inc(10); lv_timer_handler(); }

        /* Sub-mode: "settings <cat>" renders a picker */
        if (argc > 2) {
            unit_category_t cat = UNIT_CAT_TIMEZONE;
            if (strcmp(argv[2], "tz") == 0)    cat = UNIT_CAT_TIMEZONE;
            else if (strcmp(argv[2], "speed") == 0) cat = UNIT_CAT_SPEED;
            else if (strcmp(argv[2], "depth") == 0) cat = UNIT_CAT_DEPTH;
            else if (strcmp(argv[2], "temp") == 0)  cat = UNIT_CAT_TEMPERATURE;
            else if (strcmp(argv[2], "dist") == 0)  cat = UNIT_CAT_DISTANCE;
            settings.openPicker(cat);
            for (int t = 0; t < 10; t++) { lv_tick_inc(10); lv_timer_handler(); }
            return save_png("settings_picker") ? 0 : 1;
        }
        return save_png("settings") ? 0 : 1;
    } else if (render_wifi) {
        SimulatorWifiApp wifi;
        wifi.start();

        /* Run timers to trigger scan completion (1.5s = 150 ticks) */
        for (int i = 0; i < 200; i++) {
            lv_tick_inc(10);
            lv_timer_handler();
        }

        /* Check for autocomplete sub-mode: wifi-ac */
        bool autocomplete_mode = (argc > 2 && strcmp(argv[2], "ac") == 0);
        if (autocomplete_mode) {
            wifi.simulateType("Sa");
            for (int i = 0; i < 10; i++) {
                lv_tick_inc(10);
                lv_timer_handler();
            }
        }

        return save_png(autocomplete_mode ? "wifi_autocomplete" : "wifi") ? 0 : 1;
    } else if (render_kb) {
        return render_keyboard();
    } else if (render_grid) {
        return render_launcher_grid();
    } else if (render_icons_mode) {
        return render_icons();
    } else if (render_launcher) {
        /* ── Launcher connection status overlay (all states) ──────────── */
        return render_launcher_states();
    } else if (render_windrose) {
        /* ── Wind rose app (single page) ─────────────────────────────── */
        SimulatorWindRoseApp wr;
        wr.start();

        for (int i = 0; i < 20; i++) {
            lv_tick_inc(10);
            lv_timer_handler();
        }

        if (!save_png("windrose")) return 1;
    } else if (render_autopilot_menu) {
        /* ── Autopilot menu page ─────────────────────────────────────── */
        SimulatorAutopilotApp ap;
        ap.start();
        ap.openMenu();

        for (int i = 0; i < 20; i++) {
            lv_tick_inc(10);
            lv_timer_handler();
        }

        if (!save_png("autopilot-menu")) return 1;
    } else if (render_autopilot) {
        /* ── Autopilot app (single page) ──────────────────────────────── */
        SimulatorAutopilotApp ap;
        ap.start();

        for (int i = 0; i < 20; i++) {
            lv_tick_inc(10);
            lv_timer_handler();
        }

        if (!save_png("autopilot")) return 1;
    } else {
        /* ── Default: Wind Rose ───────────────────────────────────────── */
        SimulatorWindRoseApp wr;
        wr.start();

        for (int i = 0; i < 20; i++) {
            lv_tick_inc(10);
            lv_timer_handler();
        }

        if (!save_png("windrose")) return 1;
    }

    return 0;
}
