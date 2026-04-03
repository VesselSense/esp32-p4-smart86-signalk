#pragma once

#include "esp_ui.hpp"
#include "signalk_client.hpp"
#include "signalk_auth.hpp"

// ---------------------------------------------------------------------------
// AutopilotApp — autopilot display for esp-ui phone launcher.
//
// Layout (720x720, status/nav bars disabled):
//   Top strip:  Wind angle bar (horizontal scale ±30°)
//   Centre:     180° semicircular scrolling compass — heading always centred,
//               arc rotates with heading. 1:1 angular mapping (no distortion).
//               Bold red/white/green arc, tick marks, degree labels.
//               Large heading value + LH/Track/Mag labels.
//   Bottom:     Two rows of touch buttons (-1°/+1°/Ack/Menu, STBY/-10°/+10°/Auto)
//
// UI-only for now — buttons are visual, no autopilot control messages sent.
// ---------------------------------------------------------------------------

class AutopilotApp : public ESP_UI_PhoneApp {
public:
    AutopilotApp();
    ~AutopilotApp();

    void openMenu() { show_menu(); }

    enum APMode    { MODE_COMPASS, MODE_WIND, MODE_TRACK };
    enum APEngaged { ENGAGED_STBY, ENGAGED_AUTO };

protected:
    bool run()    override;
    bool back()   override;
    bool pause()  override;
    bool resume() override;

private:
    // Custom-drawn scrolling compass
    lv_obj_t *_compass    = nullptr;
    float     _heading_deg = 0.0f;
    float     _wind_angle_deg = 0.0f;
    bool      _wind_angle_valid = false;
    bool      _sk_connected = false;

    // Autopilot state
    APMode    _ap_mode    = MODE_COMPASS;
    APEngaged _ap_engaged = ENGAGED_STBY;
    float     _locked_heading    = 0.0f;
    float     _locked_wind_angle = 0.0f;

    // Labels
    lv_obj_t *_lbl_heading     = nullptr;
    lv_obj_t *_lbl_pilot_mode  = nullptr;   // "Compass" / "Wind" / "Track"
    lv_obj_t *_lbl_mode        = nullptr;   // "Track" (below heading)
    lv_obj_t *_lbl_source      = nullptr;   // "Mag" / "True"
    lv_obj_t *_lbl_lh          = nullptr;   // "LH"
    lv_obj_t *_lbl_wind_val    = nullptr;   // mode status (right label)

    // Buttons
    lv_obj_t *_btn_mode = nullptr;
    lv_obj_t *_btn_m1   = nullptr;
    lv_obj_t *_btn_p1   = nullptr;
    lv_obj_t *_btn_menu = nullptr;
    lv_obj_t *_btn_stby = nullptr;
    lv_obj_t *_btn_m10  = nullptr;
    lv_obj_t *_btn_p10  = nullptr;
    lv_obj_t *_btn_auto = nullptr;

    // Pulse animation for active STBY/Auto button + set angle marker
    lv_anim_t _pulse_anim;
    bool      _pulse_active = false;
    lv_opa_t  _pulse_opa    = LV_OPA_COVER;  // shared pulse value for marker sync

    void start_pulse(lv_obj_t *btn);
    void stop_pulse();
    static void pulse_anim_cb(void *obj, int32_t val);

    // Menu page
    enum AuthState { AUTH_NONE, AUTH_REQUESTING, AUTH_AUTHORIZED };

    lv_obj_t *_menu_page      = nullptr;
    lv_obj_t *_main_page      = nullptr;
    lv_obj_t *_lbl_server_url = nullptr;
    lv_obj_t *_lbl_server_status = nullptr;
    lv_obj_t *_lbl_auth_status = nullptr;
    lv_obj_t *_lbl_auth_info  = nullptr;
    lv_obj_t *_btn_pair       = nullptr;
    AuthState _auth_state     = AUTH_NONE;

    void show_menu();
    void hide_menu();
    static void on_menu_back(lv_event_t *e);
    static void on_pair_btn(lv_event_t *e);

    // Status overlay — shows confirmation on engage/disengage, fades after 3s
    lv_obj_t  *_overlay       = nullptr;
    lv_obj_t  *_overlay_line1 = nullptr;
    lv_obj_t  *_overlay_line2 = nullptr;
    lv_timer_t *_overlay_timer = nullptr;
    void show_overlay(const char *line1, const char *line2);
    static void overlay_fade_cb(lv_timer_t *t);

    // Rudder bar
    lv_obj_t *_wind_bar       = nullptr;
    lv_obj_t *_wind_indicator = nullptr;
    int32_t   _wind_bar_cx    = 0;
    int32_t   _wind_bar_hw    = 0;

    lv_timer_t *_update_timer = nullptr;

    void build_ui(lv_obj_t *root, int32_t w, int32_t h);
    void update_display();
    void update_ap_labels();
    static void on_update_timer(lv_timer_t *t);
    static void on_btn_event(lv_event_t *e);
    static void compass_draw_cb(lv_event_t *e);
    static void wind_bar_draw_cb(lv_event_t *e);
};
