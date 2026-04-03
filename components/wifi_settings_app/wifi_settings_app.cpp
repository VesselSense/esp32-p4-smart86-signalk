#include "wifi_settings_app.hpp"
#include "esp_log.h"
#include <string.h>
#include <ctype.h>

static const char *TAG = "wifi_app";

LV_IMG_DECLARE(wifi_icon);

/* ── Constructor ─────────────────────────────────────────────────────── */

WifiSettingsApp::WifiSettingsApp()
    : ESP_UI_PhoneApp("WiFi",
                      &wifi_icon,
                      true, false, false)
{
}

WifiSettingsApp::~WifiSettingsApp()
{
    if (_poll_timer) lv_timer_del(_poll_timer);
    ESP_LOGD(TAG, "~WifiSettingsApp");
}

/* ── Lifecycle ───────────────────────────────────────────────────────── */

bool WifiSettingsApp::run()
{
    ESP_LOGI(TAG, "WifiSettingsApp::run()");

    _saved_count = wifi_manager_load_saved(_saved, WIFI_MGR_MAX_SAVED);
    _prev_state = (wifi_mgr_state_t)-1;
    _showing_connected = false;

    build_ui();

    /* Load scan results if available */
    if (wifi_manager_scan_done()) {
        refresh_scan_results();
    } else {
        _scan_requested = true;
    }

    /* Kick a fresh scan if idle */
    if (!_scan_requested && wifi_manager_scan_start()) {
        _scan_requested = true;
    }

    /* Show correct view based on current state */
    if (wifi_manager_get_state() == WIFI_MGR_CONNECTED) {
        show_connected_view();
    } else {
        show_disconnected_view();
    }

    _poll_timer = lv_timer_create(on_poll_timer, 500, this);

    return true;
}

/* ── UI Construction ─────────────────────────────────────────────────── */

void WifiSettingsApp::build_ui()
{
    lv_obj_t *scr = lv_scr_act();

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* ── Title ───────────────────────────────────────────────────────── */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "WiFi Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* ===================================================================
     * CONNECTED VIEW — shown when WiFi is connected
     * =================================================================== */
    _connected_view = lv_obj_create(scr);
    lv_obj_set_size(_connected_view, 700, 650);
    lv_obj_align(_connected_view, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_opa(_connected_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_connected_view, 0, 0);
    lv_obj_set_style_pad_all(_connected_view, 10, 0);
    lv_obj_set_flex_flow(_connected_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_connected_view, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(_connected_view, 16, 0);
    lv_obj_add_flag(_connected_view, LV_OBJ_FLAG_HIDDEN);

    /* Current connection card */
    lv_obj_t *conn_card = lv_obj_create(_connected_view);
    lv_obj_set_size(conn_card, 680, 140);
    lv_obj_set_style_bg_color(conn_card, lv_color_hex(0x252540), 0);
    lv_obj_set_style_bg_opa(conn_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(conn_card, 0, 0);
    lv_obj_set_style_radius(conn_card, 12, 0);
    lv_obj_set_style_pad_all(conn_card, 16, 0);

    lv_obj_t *conn_title = lv_label_create(conn_card);
    lv_label_set_text(conn_title, "Connected");
    lv_obj_set_style_text_font(conn_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(conn_title, lv_color_hex(0x2ecc71), 0);
    lv_obj_align(conn_title, LV_ALIGN_TOP_LEFT, 0, 0);

    _conn_ssid_lbl = lv_label_create(conn_card);
    lv_label_set_text(_conn_ssid_lbl, "");
    lv_obj_set_style_text_font(_conn_ssid_lbl, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(_conn_ssid_lbl, lv_color_white(), 0);
    lv_obj_align(_conn_ssid_lbl, LV_ALIGN_LEFT_MID, 0, 5);

    _conn_rssi_lbl = lv_label_create(conn_card);
    lv_label_set_text(_conn_rssi_lbl, "");
    lv_obj_set_style_text_font(_conn_rssi_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_conn_rssi_lbl, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(_conn_rssi_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Disconnect button */
    lv_obj_t *disc_btn = lv_btn_create(conn_card);
    lv_obj_set_size(disc_btn, 160, 44);
    lv_obj_align(disc_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(disc_btn, lv_color_hex(0xe74c3c), 0);
    lv_obj_set_style_radius(disc_btn, 8, 0);
    lv_obj_add_event_cb(disc_btn, on_disconnect_btn, LV_EVENT_CLICKED, this);

    lv_obj_t *disc_lbl = lv_label_create(disc_btn);
    lv_label_set_text(disc_lbl, "Disconnect");
    lv_obj_set_style_text_font(disc_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(disc_lbl);

    /* Saved networks list area */
    _conn_saved_area = lv_obj_create(_connected_view);
    lv_obj_set_size(_conn_saved_area, 680, 400);
    lv_obj_set_style_bg_opa(_conn_saved_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_conn_saved_area, 0, 0);
    lv_obj_set_style_pad_all(_conn_saved_area, 0, 0);

    /* ===================================================================
     * DISCONNECTED VIEW — SSID/password input + keyboard
     * =================================================================== */
    _disconnected_view = lv_obj_create(scr);
    lv_obj_set_size(_disconnected_view, 720, 670);
    lv_obj_align(_disconnected_view, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_opa(_disconnected_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_disconnected_view, 0, 0);
    lv_obj_set_style_pad_all(_disconnected_view, 0, 0);
    lv_obj_clear_flag(_disconnected_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_disconnected_view, LV_OBJ_FLAG_HIDDEN);

    /* Top input area */
    lv_obj_t *top = lv_obj_create(_disconnected_view);
    lv_obj_set_size(top, 700, 300);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(top, 8, 0);
    lv_obj_set_style_pad_all(top, 10, 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    /* Known networks area */
    _known_area = lv_obj_create(top);
    lv_obj_set_size(_known_area, 680, 110);
    lv_obj_set_style_bg_color(_known_area, lv_color_hex(0x252540), 0);
    lv_obj_set_style_bg_opa(_known_area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_known_area, 0, 0);
    lv_obj_set_style_radius(_known_area, 10, 0);
    lv_obj_set_style_pad_all(_known_area, 8, 0);
    lv_obj_add_flag(_known_area, LV_OBJ_FLAG_HIDDEN);

    /* SSID row */
    lv_obj_t *ssid_row = lv_obj_create(top);
    lv_obj_set_size(ssid_row, 680, 44);
    lv_obj_set_style_bg_opa(ssid_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ssid_row, 0, 0);
    lv_obj_set_style_pad_all(ssid_row, 0, 0);

    lv_obj_t *ssid_lbl = lv_label_create(ssid_row);
    lv_label_set_text(ssid_lbl, "Network:");
    lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ssid_lbl, lv_color_white(), 0);
    lv_obj_align(ssid_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    _ssid_ta = lv_textarea_create(ssid_row);
    lv_textarea_set_one_line(_ssid_ta, true);
    lv_textarea_set_placeholder_text(_ssid_ta, "Enter SSID");
    lv_obj_set_style_text_font(_ssid_ta, &lv_font_montserrat_16, 0);
    lv_obj_set_width(_ssid_ta, 510);
    lv_obj_align(_ssid_ta, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_ssid_ta, on_ssid_changed, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(_ssid_ta, on_ssid_focused, LV_EVENT_FOCUSED, this);

    /* Autocomplete — on scr so it overlays */
    _autocomplete = lv_list_create(scr);
    lv_obj_set_size(_autocomplete, 510, 100);
    lv_obj_set_style_bg_color(_autocomplete, lv_color_hex(0x303050), 0);
    lv_obj_set_style_bg_opa(_autocomplete, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_autocomplete, lv_color_hex(0x505070), 0);
    lv_obj_set_style_border_width(_autocomplete, 1, 0);
    lv_obj_set_style_radius(_autocomplete, 8, 0);
    lv_obj_set_style_pad_all(_autocomplete, 4, 0);
    lv_obj_add_flag(_autocomplete, LV_OBJ_FLAG_HIDDEN);

    /* Password row */
    lv_obj_t *pw_row = lv_obj_create(top);
    lv_obj_set_size(pw_row, 680, 44);
    lv_obj_set_style_bg_opa(pw_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pw_row, 0, 0);
    lv_obj_set_style_pad_all(pw_row, 0, 0);

    lv_obj_t *pw_lbl = lv_label_create(pw_row);
    lv_label_set_text(pw_lbl, "Password:");
    lv_obj_set_style_text_font(pw_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(pw_lbl, lv_color_white(), 0);
    lv_obj_align(pw_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    _pw_ta = lv_textarea_create(pw_row);
    lv_textarea_set_one_line(_pw_ta, true);
    lv_textarea_set_placeholder_text(_pw_ta, "Enter password");
    lv_textarea_set_password_mode(_pw_ta, false);
    lv_obj_set_style_text_font(_pw_ta, &lv_font_montserrat_16, 0);
    lv_obj_set_width(_pw_ta, 510);
    lv_obj_align(_pw_ta, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_pw_ta, on_pw_focused, LV_EVENT_FOCUSED, this);

    /* Connect button + status */
    lv_obj_t *btn_row = lv_obj_create(top);
    lv_obj_set_size(btn_row, 680, 50);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);

    _connect_btn = lv_btn_create(btn_row);
    lv_obj_set_size(_connect_btn, 200, 44);
    lv_obj_align(_connect_btn, LV_ALIGN_LEFT_MID, 170, 0);
    lv_obj_set_style_bg_color(_connect_btn, lv_color_hex(0x2ecc71), 0);
    lv_obj_set_style_radius(_connect_btn, 8, 0);
    lv_obj_add_event_cb(_connect_btn, on_connect_btn, LV_EVENT_CLICKED, this);

    lv_obj_t *btn_lbl = lv_label_create(_connect_btn);
    lv_label_set_text(btn_lbl, "Connect");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(btn_lbl);

    _status_lbl = lv_label_create(btn_row);
    lv_label_set_text(_status_lbl, "");
    lv_obj_set_style_text_font(_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_status_lbl, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(_status_lbl, LV_ALIGN_LEFT_MID, 390, 0);

    /* Keyboard */
    _keyboard = lv_keyboard_create(_disconnected_view);
    lv_obj_set_size(_keyboard, 720, 370);
    lv_obj_align(_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(_keyboard, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_keyboard_set_textarea(_keyboard, _ssid_ta);
}

/* ── View switching ──────────────────────────────────────────────────── */

void WifiSettingsApp::show_connected_view()
{
    if (_showing_connected) return;
    _showing_connected = true;

    /* Update connected info */
    const char *ssid = wifi_manager_get_ssid();
    lv_label_set_text(_conn_ssid_lbl, ssid);

    /* Find RSSI for connected network in scan results */
    for (int i = 0; i < _scan_count; i++) {
        if (strcmp(_scan_results[i].ssid, ssid) == 0) {
            char rssi_buf[32];
            snprintf(rssi_buf, sizeof(rssi_buf), "Signal: %d dBm", _scan_results[i].rssi);
            lv_label_set_text(_conn_rssi_lbl, rssi_buf);
            break;
        }
    }

    /* Show other saved networks */
    lv_obj_clean(_conn_saved_area);
    int row_y = 0;

    if (_saved_count > 0) {
        lv_obj_t *saved_title = lv_label_create(_conn_saved_area);
        lv_label_set_text(saved_title, "Saved Networks");
        lv_obj_set_style_text_font(saved_title, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(saved_title, lv_color_hex(0xaaaaaa), 0);
        lv_obj_set_pos(saved_title, 10, 0);
        row_y = 28;

        for (int i = 0; i < _saved_count; i++) {
            /* Skip currently connected */
            if (strcmp(_saved[i].ssid, ssid) == 0) continue;

            lv_obj_t *row = lv_obj_create(_conn_saved_area);
            lv_obj_set_size(row, 660, 44);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x252540), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_radius(row, 8, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_set_pos(row, 10, row_y);

            lv_obj_t *lbl = lv_label_create(row);
            lv_label_set_text(lbl, _saved[i].ssid);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
            lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
            lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);

            lv_obj_t *btn = lv_btn_create(row);
            lv_obj_set_size(btn, 120, 36);
            lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -10, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x3498db), 0);
            lv_obj_set_style_radius(btn, 6, 0);
            lv_obj_set_user_data(btn, (void *)(intptr_t)i);
            lv_obj_add_event_cb(btn, on_known_connect, LV_EVENT_CLICKED, this);

            lv_obj_t *blbl = lv_label_create(btn);
            lv_label_set_text(blbl, "Switch");
            lv_obj_set_style_text_font(blbl, &lv_font_montserrat_14, 0);
            lv_obj_center(blbl);

            row_y += 52;
        }
    }

    hide_autocomplete();
    lv_obj_clear_flag(_connected_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_disconnected_view, LV_OBJ_FLAG_HIDDEN);
}

void WifiSettingsApp::show_disconnected_view()
{
    if (!_showing_connected && _disconnected_view &&
        !lv_obj_has_flag(_disconnected_view, LV_OBJ_FLAG_HIDDEN)) return;
    _showing_connected = false;

    lv_obj_add_flag(_connected_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_disconnected_view, LV_OBJ_FLAG_HIDDEN);
}

/* ── Scan result refresh ─────────────────────────────────────────────── */

void WifiSettingsApp::refresh_scan_results()
{
    _scan_count = wifi_manager_scan_get_results(_scan_results, WIFI_MGR_MAX_SCAN);
    _saved_count = wifi_manager_load_saved(_saved, WIFI_MGR_MAX_SAVED);

    if (_showing_connected) {
        show_connected_view();
    } else {
        show_known_networks();
    }

    ESP_LOGI(TAG, "Refreshed: %d scanned, %d saved", _scan_count, _saved_count);
}

/* ── Autocomplete ────────────────────────────────────────────────────── */

void WifiSettingsApp::update_autocomplete()
{
    const char *text = lv_textarea_get_text(_ssid_ta);
    int len = (int)strlen(text);

    if (len < 2 || _scan_count == 0) {
        hide_autocomplete();
        return;
    }

    lv_obj_clean(_autocomplete);

    int matches = 0;
    for (int i = 0; i < _scan_count; i++) {
        bool match = true;
        for (int c = 0; c < len; c++) {
            if (tolower((unsigned char)text[c]) !=
                tolower((unsigned char)_scan_results[i].ssid[c])) {
                match = false;
                break;
            }
        }
        if (!match) continue;

        char label[64];
        snprintf(label, sizeof(label), "%s  (%d dBm)", _scan_results[i].ssid, _scan_results[i].rssi);

        lv_obj_t *btn = lv_list_add_btn(_autocomplete, NULL, label);
        lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), lv_color_white(), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x303050), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, on_suggestion_click, LV_EVENT_CLICKED, this);
        matches++;
    }

    if (matches > 0) {
        int h = (matches > 3 ? 3 : matches) * 40 + 8;
        lv_obj_set_height(_autocomplete, h);

        lv_obj_update_layout(lv_scr_act());
        lv_coord_t ta_y = lv_obj_get_y2(_ssid_ta);
        lv_coord_t ta_x = lv_obj_get_x(_ssid_ta);
        lv_obj_set_pos(_autocomplete, ta_x, ta_y + 2);
        lv_obj_clear_flag(_autocomplete, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(_autocomplete);
    } else {
        hide_autocomplete();
    }
}

void WifiSettingsApp::hide_autocomplete()
{
    lv_obj_add_flag(_autocomplete, LV_OBJ_FLAG_HIDDEN);
}

/* ── Known networks (disconnected view — scan + saved match) ─────────── */

void WifiSettingsApp::show_known_networks()
{
    lv_obj_clean(_known_area);
    int found = 0;

    for (int i = 0; i < _scan_count && found < 2; i++) {
        for (int s = 0; s < _saved_count; s++) {
            if (strcmp(_scan_results[i].ssid, _saved[s].ssid) == 0) {
                lv_obj_t *row = lv_obj_create(_known_area);
                lv_obj_set_size(row, 640, 44);
                lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(row, 0, 0);
                lv_obj_set_style_pad_all(row, 0, 0);
                lv_obj_set_pos(row, 0, found * 50);

                char label[64];
                snprintf(label, sizeof(label), "%s  (%d dBm)", _scan_results[i].ssid, _scan_results[i].rssi);
                lv_obj_t *lbl = lv_label_create(row);
                lv_label_set_text(lbl, label);
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
                lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
                lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 10, 0);

                lv_obj_t *btn = lv_btn_create(row);
                lv_obj_set_size(btn, 120, 36);
                lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -10, 0);
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x3498db), 0);
                lv_obj_set_style_radius(btn, 6, 0);
                lv_obj_set_user_data(btn, (void *)(intptr_t)s);
                lv_obj_add_event_cb(btn, on_known_connect, LV_EVENT_CLICKED, this);

                lv_obj_t *blbl = lv_label_create(btn);
                lv_label_set_text(blbl, "Connect");
                lv_obj_set_style_text_font(blbl, &lv_font_montserrat_14, 0);
                lv_obj_center(blbl);

                found++;
                break;
            }
        }
    }

    if (found > 0) {
        int h = found * 50 + 10;
        lv_obj_set_height(_known_area, h);
        lv_obj_clear_flag(_known_area, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_known_area, LV_OBJ_FLAG_HIDDEN);
    }
}

void WifiSettingsApp::set_status(const char *text, lv_color_t color)
{
    lv_label_set_text(_status_lbl, text);
    lv_obj_set_style_text_color(_status_lbl, color, 0);
}

/* ── Event Callbacks ─────────────────────────────────────────────────── */

void WifiSettingsApp::on_ssid_changed(lv_event_t *e)
{
    auto *app = (WifiSettingsApp *)lv_event_get_user_data(e);
    app->update_autocomplete();
}

void WifiSettingsApp::on_ssid_focused(lv_event_t *e)
{
    auto *app = (WifiSettingsApp *)lv_event_get_user_data(e);
    lv_keyboard_set_textarea(app->_keyboard, app->_ssid_ta);
}

void WifiSettingsApp::on_pw_focused(lv_event_t *e)
{
    auto *app = (WifiSettingsApp *)lv_event_get_user_data(e);
    lv_keyboard_set_textarea(app->_keyboard, app->_pw_ta);
    app->hide_autocomplete();
}

void WifiSettingsApp::on_suggestion_click(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    auto *app = (WifiSettingsApp *)lv_event_get_user_data(e);

    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (idx < 0 || idx >= app->_scan_count) return;

    const char *ssid = app->_scan_results[idx].ssid;

    lv_textarea_set_text(app->_ssid_ta, ssid);
    app->hide_autocomplete();

    for (int i = 0; i < app->_saved_count; i++) {
        if (strcmp(app->_saved[i].ssid, ssid) == 0) {
            lv_textarea_set_text(app->_pw_ta, app->_saved[i].password);
            break;
        }
    }

    lv_event_send(app->_pw_ta, LV_EVENT_FOCUSED, NULL);
}

void WifiSettingsApp::on_connect_btn(lv_event_t *e)
{
    auto *app = (WifiSettingsApp *)lv_event_get_user_data(e);
    const char *ssid = lv_textarea_get_text(app->_ssid_ta);
    const char *pw   = lv_textarea_get_text(app->_pw_ta);

    if (strlen(ssid) == 0) {
        app->set_status("Enter a network name", lv_color_hex(0xe74c3c));
        return;
    }

    if (!wifi_manager_connect(ssid, pw)) {
        app->set_status("Busy — try again", lv_color_hex(0xe74c3c));
        return;
    }

    ESP_LOGI(TAG, "Connect: SSID=%s", ssid);
    app->set_status("Connecting...", lv_color_hex(0xf39c12));
}

void WifiSettingsApp::on_disconnect_btn(lv_event_t *e)
{
    auto *app = (WifiSettingsApp *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "User disconnect");
    wifi_manager_disconnect();
    app->show_disconnected_view();
}

void WifiSettingsApp::on_known_connect(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    auto *app = (WifiSettingsApp *)lv_event_get_user_data(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);

    if (idx >= 0 && idx < app->_saved_count) {
        ESP_LOGI(TAG, "Connect to saved: %s", app->_saved[idx].ssid);

        if (wifi_manager_connect(app->_saved[idx].ssid, app->_saved[idx].password)) {
            app->set_status("Connecting...", lv_color_hex(0xf39c12));
        } else {
            app->set_status("Busy — try again", lv_color_hex(0xe74c3c));
        }
    }
}

/* ── Poll timer — switches view based on connection state ────────────── */

void WifiSettingsApp::on_poll_timer(lv_timer_t *t)
{
    auto *app = (WifiSettingsApp *)t->user_data;

    /* Check if scan completed */
    if (app->_scan_requested && wifi_manager_scan_done()) {
        app->_scan_requested = false;
        app->refresh_scan_results();
    }

    /* Check connection state changes */
    wifi_mgr_state_t state = wifi_manager_get_state();

    if (state != app->_prev_state) {
        app->_prev_state = state;
        switch (state) {
        case WIFI_MGR_CONNECTED: {
            /* Save credentials */
            if (!app->_showing_connected) {
                const char *ssid = lv_textarea_get_text(app->_ssid_ta);
                const char *pw   = lv_textarea_get_text(app->_pw_ta);
                if (strlen(ssid) > 0) {
                    wifi_manager_save_network(ssid, pw);
                }
            }
            /* Refresh saved list and switch to connected view */
            app->_saved_count = wifi_manager_load_saved(app->_saved, WIFI_MGR_MAX_SAVED);
            app->_showing_connected = false;  /* force refresh */
            app->show_connected_view();
            break;
        }
        case WIFI_MGR_CONNECT_FAILED: {
            const char *err = wifi_manager_get_error();
            app->set_status(err[0] ? err : "Connection failed", lv_color_hex(0xe74c3c));
            app->show_disconnected_view();
            break;
        }
        case WIFI_MGR_CONNECTING:
            app->set_status("Connecting...", lv_color_hex(0xf39c12));
            break;
        case WIFI_MGR_DISCONNECTED:
            app->set_status("Disconnected", lv_color_hex(0xe74c3c));
            app->show_disconnected_view();
            break;
        case WIFI_MGR_SCANNING:
            break;
        }
    }
}
