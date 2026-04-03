#pragma once

#include "esp_ui.hpp"
#include "lvgl.h"
#include "unit_config.hpp"

/* Number of categories shown in settings */
#define SETTINGS_VISIBLE_CATS 7  /* TZ, Speed, Depth, Temp, Distance, Pressure, Volume */

class SettingsApp : public ESP_UI_PhoneApp {
public:
    SettingsApp();
    ~SettingsApp() override;

    bool run()    override;
    bool back()   override;
    bool pause()  override { return true; }
    bool resume() override { return true; }

protected:
    void build_ui();
    void show_main_view();
    void show_picker(unit_category_t cat);
    void populate_picker();
    void update_row_labels();

    static void on_row_click(lv_event_t *e);
    static void on_picker_select(lv_event_t *e);
    static void on_picker_back(lv_event_t *e);

    lv_obj_t *_main_view       = nullptr;
    lv_obj_t *_picker_view     = nullptr;
    lv_obj_t *_picker_title    = nullptr;
    lv_obj_t *_picker_list     = nullptr;

    /* One value label per settings row */
    lv_obj_t *_row_value_lbl[SETTINGS_VISIBLE_CATS] = {};

    /* Which categories to show, in order */
    static const unit_category_t s_visible_cats[SETTINGS_VISIBLE_CATS];

    unit_category_t _picker_category = UNIT_CAT_TIMEZONE;
    bool _showing_picker = false;

private:
};
