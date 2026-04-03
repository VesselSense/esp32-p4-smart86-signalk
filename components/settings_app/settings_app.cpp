#include "settings_app.hpp"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "settings";

LV_IMG_DECLARE(settings_gear_icon);

/* Categories displayed in settings, in order */
const unit_category_t SettingsApp::s_visible_cats[SETTINGS_VISIBLE_CATS] = {
    UNIT_CAT_TIMEZONE,
    UNIT_CAT_SPEED,
    UNIT_CAT_DEPTH,
    UNIT_CAT_TEMPERATURE,
    UNIT_CAT_DISTANCE,
    UNIT_CAT_PRESSURE,
    UNIT_CAT_VOLUME,
};

/* ── Constructor ─────────────────────────────────────────────────────── */

SettingsApp::SettingsApp()
    : ESP_UI_PhoneApp("Settings",
                      &settings_gear_icon,
                      true, false, false)
{
}

SettingsApp::~SettingsApp()
{
    ESP_LOGD(TAG, "~SettingsApp");
}

/* ── Lifecycle ───────────────────────────────────────────────────────── */

bool SettingsApp::run()
{
    ESP_LOGI(TAG, "SettingsApp::run()");
    _showing_picker = false;
    build_ui();
    show_main_view();
    return true;
}

bool SettingsApp::back()
{
    if (_showing_picker) {
        show_main_view();
        return true;
    }
    return notifyCoreClosed();
}

/* ── UI Construction ─────────────────────────────────────────────────── */

void SettingsApp::build_ui()
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* ===== MAIN VIEW ===== */
    _main_view = lv_obj_create(scr);
    lv_obj_set_size(_main_view, 720, 720);
    lv_obj_align(_main_view, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(_main_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_main_view, 0, 0);
    lv_obj_set_style_pad_all(_main_view, 0, 0);

    lv_obj_t *title = lv_label_create(_main_view);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    /* Settings rows */
    for (int i = 0; i < SETTINGS_VISIBLE_CATS; i++) {
        unit_category_t cat = s_visible_cats[i];
        int y = 70 + i * 68;

        lv_obj_t *row = lv_obj_create(_main_view);
        lv_obj_set_size(row, 680, 58);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x252540), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(row, (void *)(intptr_t)cat);
        lv_obj_add_event_cb(row, on_row_click, LV_EVENT_CLICKED, this);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, unit_category_name(cat));
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 16, 0);

        _row_value_lbl[i] = lv_label_create(row);
        lv_label_set_text(_row_value_lbl[i], unit_option_name(cat));
        lv_obj_set_style_text_font(_row_value_lbl[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_row_value_lbl[i], lv_color_hex(0x888888), 0);
        lv_obj_align(_row_value_lbl[i], LV_ALIGN_RIGHT_MID, -40, 0);

        lv_obj_t *chevron = lv_label_create(row);
        lv_label_set_text(chevron, ">");
        lv_obj_set_style_text_font(chevron, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(chevron, lv_color_hex(0x555555), 0);
        lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -12, 0);
    }

    /* ===== PICKER VIEW ===== */
    _picker_view = lv_obj_create(scr);
    lv_obj_set_size(_picker_view, 720, 720);
    lv_obj_align(_picker_view, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_picker_view, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(_picker_view, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_picker_view, 0, 0);
    lv_obj_set_style_pad_all(_picker_view, 0, 0);
    lv_obj_clear_flag(_picker_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_picker_view, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *back_btn = lv_btn_create(_picker_view);
    lv_obj_set_size(back_btn, 80, 40);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, 12);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x3498db), 0);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, on_picker_back, LV_EVENT_CLICKED, this);

    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(back_lbl);

    _picker_title = lv_label_create(_picker_view);
    lv_label_set_text(_picker_title, "");
    lv_obj_set_style_text_font(_picker_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_picker_title, lv_color_white(), 0);
    lv_obj_align(_picker_title, LV_ALIGN_TOP_MID, 0, 18);

    _picker_list = lv_list_create(_picker_view);
    lv_obj_set_size(_picker_list, 700, 650);
    lv_obj_align(_picker_list, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_color(_picker_list, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(_picker_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_picker_list, 0, 0);
    lv_obj_set_style_pad_all(_picker_list, 4, 0);
}

/* ── View switching ──────────────────────────────────────────────────── */

void SettingsApp::show_main_view()
{
    _showing_picker = false;
    update_row_labels();
    lv_obj_add_flag(_picker_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_main_view, LV_OBJ_FLAG_HIDDEN);
}

void SettingsApp::show_picker(unit_category_t cat)
{
    _picker_category = cat;
    _showing_picker = true;

    lv_label_set_text(_picker_title, unit_category_name(cat));
    populate_picker();

    lv_obj_add_flag(_main_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_picker_view, LV_OBJ_FLAG_HIDDEN);
}

void SettingsApp::populate_picker()
{
    lv_obj_clean(_picker_list);

    int count = unit_option_count(_picker_category);
    int current = unit_config_get(_picker_category);

    for (int i = 0; i < count; i++) {
        char label[64];
        if (i == current) {
            snprintf(label, sizeof(label), LV_SYMBOL_OK "  %s", unit_option_name_at(_picker_category, i));
        } else {
            snprintf(label, sizeof(label), "    %s", unit_option_name_at(_picker_category, i));
        }

        lv_obj_t *btn = lv_list_add_btn(_picker_list, NULL, label);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x252540), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), lv_color_white(), 0);
        lv_obj_set_style_text_font(lv_obj_get_child(btn, 0), &lv_font_montserrat_24, 0);
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, on_picker_select, LV_EVENT_CLICKED, this);
    }
}

void SettingsApp::update_row_labels()
{
    for (int i = 0; i < SETTINGS_VISIBLE_CATS; i++) {
        if (_row_value_lbl[i]) {
            lv_label_set_text(_row_value_lbl[i], unit_option_name(s_visible_cats[i]));
        }
    }
}

/* ── Event Callbacks ─────────────────────────────────────────────────── */

void SettingsApp::on_row_click(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    auto *app = (SettingsApp *)lv_event_get_user_data(e);
    unit_category_t cat = (unit_category_t)(intptr_t)lv_obj_get_user_data(row);
    app->show_picker(cat);
}

void SettingsApp::on_picker_select(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    auto *app = (SettingsApp *)lv_event_get_user_data(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);

    unit_config_set(app->_picker_category, idx);
    ESP_LOGI(TAG, "Set %s = %s", unit_category_name(app->_picker_category),
             unit_option_name(app->_picker_category));

    app->show_main_view();
}

void SettingsApp::on_picker_back(lv_event_t *e)
{
    auto *app = (SettingsApp *)lv_event_get_user_data(e);
    app->show_main_view();
}
