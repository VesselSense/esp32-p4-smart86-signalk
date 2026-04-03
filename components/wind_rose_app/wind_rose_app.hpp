#pragma once

#include "esp_ui.hpp"
#include "signalk_client.hpp"

class WindRoseApp : public ESP_UI_PhoneApp {
public:
    WindRoseApp();
    ~WindRoseApp();

protected:
    bool run()    override;
    bool back()   override;
    bool pause()  override;
    bool resume() override;

private:
    static constexpr int LABEL_COUNT = 12;  // every 30°

    lv_obj_t          *_meter       = nullptr;
    lv_meter_scale_t  *_scale       = nullptr;
    lv_timer_t        *_update_timer = nullptr;
    bool _sk_connected = false;

    // Labels on the ring — repositioned when heading changes
    lv_obj_t *_labels[LABEL_COUNT] = {};
    int       _label_deg[LABEL_COUNT] = {};  // compass bearing for each label

    int32_t   _meter_r  = 0;
    int32_t   _label_r  = 0;
    int32_t   _cx       = 0;
    int32_t   _cy       = 0;

    // Speed display on inner disc (AWS)
    lv_obj_t *_lbl_speed_int  = nullptr;
    lv_obj_t *_lbl_speed_frac = nullptr;

    // TWS floating overlay — independent of inner disc
    lv_obj_t *_tws_container  = nullptr;
    lv_obj_t *_lbl_tws_title  = nullptr;
    lv_obj_t *_lbl_tws_value  = nullptr;
    lv_obj_t *_lbl_tws_unit   = nullptr;

    // Wind marker triangles — drawn via custom callback on a full-screen
    // canvas object (same pattern as autopilot compass_draw_cb)
    lv_obj_t *_canvas   = nullptr;
    float     _awa_deg  = 0.0f;
    float     _twa_deg  = 0.0f;
    bool      _awa_valid = false;
    bool      _twa_valid = false;

    // Corner data panels — generic cells that can display any instrument value
    struct CornerCell {
        lv_obj_t *container  = nullptr;
        lv_obj_t *lbl_title  = nullptr;
        lv_obj_t *lbl_value  = nullptr;
        lv_obj_t *lbl_unit   = nullptr;
        lv_obj_t *lbl_degree = nullptr;   // °  positioned top-right of value
    };
    static constexpr int CORNER_COUNT = 4;
    CornerCell _corners[CORNER_COUNT];

    void create_corner_cell(int idx, lv_obj_t *parent,
                            int32_t x, int32_t y, int32_t w, int32_t h);
    void set_corner_cell(int idx, const char *title,
                         const char *value, const char *unit);

    void reposition_labels(float heading_deg);
    static void on_update_timer(lv_timer_t *t);
    static void marker_draw_cb(lv_event_t *e);
};
