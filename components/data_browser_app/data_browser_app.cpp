#include "data_browser_app.hpp"
#include "unit_config.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

#ifndef SIMULATOR_BUILD
#include "signalk_auth.hpp"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static const char *TAG = "browser";

LV_IMG_DECLARE(esp_ui_phone_app_launcher_image_default);

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

DataBrowserApp::DataBrowserApp()
    : ESP_UI_PhoneApp("Data",
                      &esp_ui_phone_app_launcher_image_default,
                      true, false, false)
{
}

DataBrowserApp::~DataBrowserApp()
{
    ESP_LOGD(TAG, "~DataBrowserApp");
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void DataBrowserApp::extract_group(const char *path, char *out, int out_len)
{
    const char *dot = strchr(path, '.');
    int len = dot ? (int)(dot - path) : (int)strlen(path);
    if (len >= out_len) len = out_len - 1;
    memcpy(out, path, len);
    out[len] = '\0';
    if (out[0]) out[0] = toupper((unsigned char)out[0]);
}

int DataBrowserApp::path_compare(const void *a, const void *b)
{
    auto *pa = (const PathEntry *)a;
    auto *pb = (const PathEntry *)b;
    int g = strcmp(pa->group, pb->group);
    if (g != 0) return g;
    return strcmp(pa->path, pb->path);
}

void DataBrowserApp::format_value(const PathValue *pv, const char *units,
                                   char *buf, int buf_len,
                                   char *unit_buf, int unit_len)
{
    if (!pv || !pv->valid) {
        snprintf(buf, buf_len, "--");
        unit_buf[0] = '\0';
        return;
    }

    float v = pv->value;

    if (strcmp(units, "m/s") == 0) {
        snprintf(buf, buf_len, "%.1f", unit_convert(UNIT_CAT_SPEED, v));
        snprintf(unit_buf, unit_len, "%s", unit_label_long(UNIT_CAT_SPEED));
    } else if (strcmp(units, "rad") == 0) {
        float deg = v * 57.2957795f;
        if (deg < 0) deg += 360.0f;
        snprintf(buf, buf_len, "%.0f", fmodf(deg, 360.0f));
        snprintf(unit_buf, unit_len, "°");
    } else if (strcmp(units, "rad/s") == 0) {
        snprintf(buf, buf_len, "%.3f", v * 57.2957795f);
        snprintf(unit_buf, unit_len, "°/s");
    } else if (strcmp(units, "K") == 0) {
        snprintf(buf, buf_len, "%.1f", unit_convert(UNIT_CAT_TEMPERATURE, v));
        snprintf(unit_buf, unit_len, "%s", unit_label_long(UNIT_CAT_TEMPERATURE));
    } else if (strcmp(units, "m") == 0) {
        snprintf(buf, buf_len, "%.1f", unit_convert(UNIT_CAT_DEPTH, v));
        snprintf(unit_buf, unit_len, "%s", unit_label_long(UNIT_CAT_DEPTH));
    } else if (strcmp(units, "Pa") == 0 || strcmp(units, "kPa") == 0) {
        float pa = (strcmp(units, "kPa") == 0) ? v * 1000.0f : v;
        snprintf(buf, buf_len, "%.1f", unit_convert(UNIT_CAT_PRESSURE, pa));
        snprintf(unit_buf, unit_len, "%s", unit_label_long(UNIT_CAT_PRESSURE));
    } else if (strcmp(units, "ratio") == 0) {
        snprintf(buf, buf_len, "%.0f", v * 100.0f);
        snprintf(unit_buf, unit_len, "%%");
    } else if (strcmp(units, "V") == 0) {
        snprintf(buf, buf_len, "%.2f", v);
        snprintf(unit_buf, unit_len, "V");
    } else if (strcmp(units, "A") == 0) {
        snprintf(buf, buf_len, "%.1f", v);
        snprintf(unit_buf, unit_len, "A");
    } else if (strcmp(units, "W") == 0) {
        snprintf(buf, buf_len, "%.0f", v);
        snprintf(unit_buf, unit_len, "W");
    } else {
        snprintf(buf, buf_len, "%.2f", v);
        snprintf(unit_buf, unit_len, "%s", units);
    }
}

// ---------------------------------------------------------------------------
// HTTP path discovery (device only)
// ---------------------------------------------------------------------------

#ifndef SIMULATOR_BUILD

struct HttpBuf {
    char  *data;
    size_t len;
    size_t cap;
};

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    HttpBuf *buf = (HttpBuf *)evt->user_data;
    if (!buf) return ESP_OK;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (buf->len + evt->data_len < buf->cap) {
            memcpy(buf->data + buf->len, evt->data, evt->data_len);
            buf->len += evt->data_len;
            buf->data[buf->len] = '\0';
        }
    }
    return ESP_OK;
}

void DataBrowserApp::walk_json(cJSON *obj, char *prefix, int prefix_len)
{
    if (!cJSON_IsObject(obj)) return;

    cJSON *child;
    cJSON_ArrayForEach(child, obj) {
        const char *key = child->string;
        if (!key) continue;

        // Skip meta/internal keys
        if (strcmp(key, "meta") == 0 || strcmp(key, "$source") == 0 ||
            strcmp(key, "timestamp") == 0 || strcmp(key, "values") == 0 ||
            strcmp(key, "pgn") == 0 || strcmp(key, "mmsi") == 0 ||
            strcmp(key, "name") == 0 || strcmp(key, "uuid") == 0) continue;

        // Build full path
        char path[128];
        if (prefix_len > 0) {
            snprintf(path, sizeof(path), "%.*s.%s", prefix_len, prefix, key);
        } else {
            snprintf(path, sizeof(path), "%s", key);
        }
        int path_len = (int)strlen(path);

        // Check if this node has a "value" key — it's a leaf
        cJSON *val = cJSON_GetObjectItem(child, "value");
        if (val) {
            if (_path_count >= MAX_PATHS) return;
            PathEntry &e = _paths[_path_count];
            strncpy(e.path, path, sizeof(e.path) - 1);
            e.path[sizeof(e.path) - 1] = '\0';

            // displayName from meta
            e.display_name[0] = '\0';
            cJSON *meta = cJSON_GetObjectItem(child, "meta");
            if (meta) {
                cJSON *dn = cJSON_GetObjectItem(meta, "displayName");
                if (cJSON_IsString(dn) && dn->valuestring[0]) {
                    strncpy(e.display_name, dn->valuestring, sizeof(e.display_name) - 1);
                }
                cJSON *u = cJSON_GetObjectItem(meta, "units");
                if (cJSON_IsString(u)) {
                    strncpy(e.units, u->valuestring, sizeof(e.units) - 1);
                } else {
                    e.units[0] = '\0';
                }
            } else {
                e.units[0] = '\0';
            }

            e.is_numeric = cJSON_IsNumber(val);
            extract_group(e.path, e.group, sizeof(e.group));
            _path_count++;
        } else if (cJSON_IsObject(child)) {
            // Recurse into nested objects
            walk_json(child, path, path_len);
        }
    }
}

void DataBrowserApp::fetch_task(void *arg)
{
    auto *app = (DataBrowserApp *)arg;

    const char *base = signalk_auth_get_base_url();
    if (!base) {
        ESP_LOGE(TAG, "No base URL — cannot fetch paths");
        app->_fetch_done = true;
        lv_async_call(on_fetch_complete, app);
        vTaskDelete(NULL);
        return;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s/signalk/v1/api/vessels/self", base);

    // Allocate large buffer on PSRAM — response can be 200KB+
    size_t buf_size = 256 * 1024;
    char *resp = (char *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!resp) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        app->_fetch_done = true;
        lv_async_call(on_fetch_complete, app);
        vTaskDelete(NULL);
        return;
    }
    resp[0] = '\0';

    HttpBuf hbuf = { resp, 0, buf_size - 1 };

    ESP_LOGI(TAG, "Fetching paths from %s", url);

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_GET;
    cfg.event_handler = http_event_handler;
    cfg.user_data = &hbuf;
    cfg.timeout_ms = 15000;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 1024;
    cfg.skip_cert_common_name_check = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status == 200 && hbuf.len > 0) {
        ESP_LOGI(TAG, "Fetched %d bytes from %s", (int)hbuf.len, url);

        cJSON *root = cJSON_Parse(resp);
        if (root) {
            char prefix[1] = {};
            app->walk_json(root, prefix, 0);
            cJSON_Delete(root);

            // Sort by group then path
            if (app->_path_count > 1) {
                qsort(app->_paths, app->_path_count, sizeof(PathEntry), path_compare);
            }
            ESP_LOGI(TAG, "Discovered %d paths", app->_path_count);
        } else {
            ESP_LOGE(TAG, "JSON parse failed");
        }
    } else {
        ESP_LOGE(TAG, "HTTP fetch failed: err=%d status=%d len=%d", err, status, (int)hbuf.len);
    }

    heap_caps_free(resp);
    app->_fetch_done = true;
    lv_async_call(on_fetch_complete, app);
    vTaskDelete(NULL);
}

#else
// ---------------------------------------------------------------------------
// Simulator stub paths
// ---------------------------------------------------------------------------

void DataBrowserApp::load_stub_paths()
{
    struct StubPath {
        const char *path;
        const char *display_name;
        const char *units;
        bool is_numeric;
    };

    static const StubPath stubs[] = {
        {"electrical.batteries.288.voltage",          "House Battery Voltage",   "V",     true},
        {"electrical.batteries.288.current",          "",                        "A",     true},
        {"electrical.batteries.288.capacity.stateOfCharge", "",                  "ratio", true},
        {"electrical.batteries.288-second.voltage",   "Start Battery Voltage",   "V",     true},
        {"electrical.pumps.bilge.1.voltage",          "Analog input voltage",    "V",     true},
        {"electrical.solar.289.panelPower",           "",                        "W",     true},
        {"electrical.solar.289.voltage",              "",                        "V",     true},
        {"electrical.solar.289.current",              "",                        "A",     true},
        {"electrical.switches.charger.temperature",   "Charger Relay Temperature", "K",   true},
        {"environment.bilge.aft.flood",               "Bilge Flood Level",       "ratio", true},
        {"environment.bilge.forward.flood",           "Bilge Flood Level",       "ratio", true},
        {"environment.depth.belowTransducer",         "",                        "m",     true},
        {"environment.inside.humidity",               "Cabin Humidity",          "%",     true},
        {"environment.inside.pressure",               "Cabin Pressure",          "kPa",   true},
        {"environment.inside.temperature",            "Cabin Temperature",       "K",     true},
        {"environment.refrigerator.humidity",         "Refrigerator Humidity",   "%",     true},
        {"environment.refrigerator.temperature",      "Refrigerator Temperature","K",     true},
        {"environment.water.temperature",             "",                        "K",     true},
        {"environment.wind.angleApparent",            "",                        "rad",   true},
        {"environment.wind.speedApparent",            "",                        "m/s",   true},
        {"navigation.courseOverGroundTrue",            "",                        "rad",   true},
        {"navigation.headingMagnetic",                "",                        "rad",   true},
        {"navigation.log",                            "",                        "m",     true},
        {"navigation.position",                       "",                        "deg",   false},
        {"navigation.rateOfTurn",                     "",                        "rad/s", true},
        {"navigation.speedOverGround",                "",                        "m/s",   true},
        {"navigation.speedThroughWater",              "",                        "m/s",   true},
        {"navigation.trip.log",                       "",                        "m",     true},
        {"navigation.gnss.satellites",                "",                        "",      true},
        {"steering.autopilot.state",                  "",                        "",      false},
        {"steering.rudderAngle",                      "",                        "rad",   true},
        {"tanks.freshWater.main.currentLevel",        "Tank freshwater level",   "ratio", true},
        {"tanks.freshWater.main.capacity",            "",                        "m3",    true},
        {"computer.nmea.cpu.temperature",             "",                        "K",     true},
        {"computer.nmea.memory.utilisation",          "NMEA Computer Memory Usage","ratio",true},
    };

    _path_count = 0;
    for (int i = 0; i < (int)(sizeof(stubs) / sizeof(stubs[0])); i++) {
        if (_path_count >= MAX_PATHS) break;
        PathEntry &e = _paths[_path_count];
        strncpy(e.path, stubs[i].path, sizeof(e.path) - 1);
        if (stubs[i].display_name[0]) {
            strncpy(e.display_name, stubs[i].display_name, sizeof(e.display_name) - 1);
        } else {
            e.display_name[0] = '\0';
        }
        strncpy(e.units, stubs[i].units, sizeof(e.units) - 1);
        e.is_numeric = stubs[i].is_numeric;
        extract_group(e.path, e.group, sizeof(e.group));
        _path_count++;
    }
}
#endif

// ---------------------------------------------------------------------------
// UI Construction
// ---------------------------------------------------------------------------

void DataBrowserApp::build_ui(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // ===== LIST VIEW =====
    _list_view = lv_obj_create(screen);
    lv_obj_set_size(_list_view, 720, 720);
    lv_obj_align(_list_view, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(_list_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_list_view, 0, 0);
    lv_obj_set_style_pad_all(_list_view, 0, 0);
    lv_obj_clear_flag(_list_view, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(_list_view);
    lv_label_set_text(title, "SignalK Data");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    _status_lbl = lv_label_create(_list_view);
    lv_label_set_text(_status_lbl, "Loading...");
    lv_obj_set_style_text_font(_status_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_status_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align(_status_lbl, LV_ALIGN_CENTER, 0, 0);

    _path_list = lv_list_create(_list_view);
    lv_obj_set_size(_path_list, 700, 650);
    lv_obj_align(_path_list, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(_path_list, lv_color_hex(0x111122), 0);
    lv_obj_set_style_bg_opa(_path_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_path_list, 0, 0);
    lv_obj_set_style_pad_all(_path_list, 4, 0);
    lv_obj_set_style_pad_row(_path_list, 6, 0);
    lv_obj_add_flag(_path_list, LV_OBJ_FLAG_HIDDEN);  // hidden until paths loaded

    // ===== DETAIL VIEW =====
    _detail_view = lv_obj_create(screen);
    lv_obj_set_size(_detail_view, 720, 720);
    lv_obj_align(_detail_view, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_detail_view, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(_detail_view, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_detail_view, 0, 0);
    lv_obj_set_style_pad_all(_detail_view, 0, 0);
    lv_obj_clear_flag(_detail_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_detail_view, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *back_btn = lv_btn_create(_detail_view);
    lv_obj_set_size(back_btn, 80, 40);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, 12);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x3498db), 0);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, on_back_click, LV_EVENT_CLICKED, this);

    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(back_lbl);

    // Name/path at top
    _detail_name = lv_label_create(_detail_view);
    lv_label_set_text(_detail_name, "");
    lv_obj_set_style_text_font(_detail_name, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_detail_name, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_width(_detail_name, 600);
    lv_obj_set_style_text_align(_detail_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_detail_name, LV_LABEL_LONG_WRAP);
    lv_obj_align(_detail_name, LV_ALIGN_TOP_MID, 0, 70);

    _detail_path = lv_label_create(_detail_view);
    lv_label_set_text(_detail_path, "");
    lv_obj_set_style_text_font(_detail_path, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_detail_path, lv_color_hex(0x444444), 0);
    lv_obj_set_style_text_align(_detail_path, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(_detail_path, 680);
    lv_obj_align(_detail_path, LV_ALIGN_TOP_MID, 0, 110);

    // Large value — centered on screen
    LV_FONT_DECLARE(lv_font_montserrat_120);
    _detail_value = lv_label_create(_detail_view);
    lv_label_set_text(_detail_value, "--");
    lv_obj_set_style_text_font(_detail_value, &lv_font_montserrat_120, 0);
    lv_obj_set_style_text_color(_detail_value, lv_color_white(), 0);
    lv_obj_align(_detail_value, LV_ALIGN_CENTER, 0, 0);

    // Unit label — overlays near value
    _detail_unit = lv_label_create(_detail_view);
    lv_label_set_text(_detail_unit, "");
    lv_obj_set_style_text_font(_detail_unit, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(_detail_unit, lv_color_hex(0x888888), 0);
}

// ---------------------------------------------------------------------------
// List population
// ---------------------------------------------------------------------------

void DataBrowserApp::populate_list()
{
    lv_obj_clean(_path_list);

    char current_group[32] = {};

    for (int i = 0; i < _path_count; i++) {
        if (strcmp(_paths[i].group, current_group) != 0) {
            strncpy(current_group, _paths[i].group, sizeof(current_group) - 1);

            lv_obj_t *hdr = lv_list_add_text(_path_list, current_group);
            lv_obj_set_style_text_color(hdr, lv_color_hex(0x3498db), 0);
            lv_obj_set_style_text_font(hdr, &lv_font_montserrat_16, 0);
            lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
            lv_obj_set_style_pad_top(hdr, 12, 0);
            lv_obj_set_style_pad_bottom(hdr, 2, 0);
        }

        bool has_name = _paths[i].display_name[0] != '\0';
        const char *row_title = has_name ? _paths[i].display_name : _paths[i].path;

        lv_obj_t *row = lv_obj_create(_path_list);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1e1e38), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 12, 0);
        lv_obj_set_style_pad_row(row, 2, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(row, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, on_path_click, LV_EVENT_CLICKED, this);

        if (has_name) {
            lv_obj_t *name_lbl = lv_label_create(row);
            lv_label_set_text(name_lbl, _paths[i].display_name);
            lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(name_lbl, lv_color_white(), 0);
            lv_obj_set_width(name_lbl, lv_pct(100));
        }

        lv_obj_t *path_lbl = lv_label_create(row);
        lv_label_set_text(path_lbl, _paths[i].path);
        lv_obj_set_style_text_font(path_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(path_lbl,
            has_name ? lv_color_hex(0x5a5a7a) : lv_color_white(), 0);
        lv_obj_set_width(path_lbl, lv_pct(100));

        if (!_paths[i].is_numeric) {
            lv_obj_set_style_text_color(path_lbl, lv_color_hex(0x666666), 0);
        }
    }
}

// ---------------------------------------------------------------------------
// View switching
// ---------------------------------------------------------------------------

void DataBrowserApp::show_list_view()
{
    // Clear watch + unsubscribe before switching view
    if (_showing_detail && _selected_path[0]) {
        signalk_client_watch(NULL, NULL, NULL);
        signalk_client_unsubscribe(_selected_path);
        _selected_path[0] = '\0';
    }
    _showing_detail = false;
    lv_obj_add_flag(_detail_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_list_view, LV_OBJ_FLAG_HIDDEN);
}

void DataBrowserApp::show_detail_view(int path_idx)
{
    if (path_idx < 0 || path_idx >= _path_count) return;
    if (!_paths[path_idx].is_numeric) return;  // skip non-numeric paths

    const PathEntry &e = _paths[path_idx];
    strncpy(_selected_path, e.path, sizeof(_selected_path) - 1);
    strncpy(_selected_units, e.units, sizeof(_selected_units) - 1);
    _showing_detail = true;

    lv_label_set_text(_detail_name, e.display_name[0] ? e.display_name : e.path);
    lv_label_set_text(_detail_path, e.display_name[0] ? e.path : "");
    lv_label_set_text(_detail_value, "--");
    lv_label_set_text(_detail_unit, "");

    lv_obj_add_flag(_list_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_detail_view, LV_OBJ_FLAG_HIDDEN);

    // Subscribe + watch for live updates
    ESP_LOGI(TAG, "Detail view: subscribing + watching: %s", _selected_path);
    signalk_client_subscribe(_selected_path, 1000);
    signalk_client_watch(_selected_path, on_value_changed, this);
}

#ifndef SIMULATOR_BUILD
void DataBrowserApp::try_start_fetch()
{
    const char *base = signalk_auth_get_base_url();
    if (base) {
        // Server discovered — start fetch
        if (_wait_timer) {
            lv_timer_del(_wait_timer);
            _wait_timer = nullptr;
        }
        lv_label_set_text(_status_lbl, "Loading...");
        xTaskCreate(fetch_task, "sk_fetch", 16384, this, 5, NULL);
    } else {
        // Not ready yet — poll every 500ms
        lv_label_set_text(_status_lbl, "Waiting for server...");
        if (!_wait_timer) {
            _wait_timer = lv_timer_create(on_wait_timer, 500, this);
        }
    }
}

void DataBrowserApp::on_wait_timer(lv_timer_t *t)
{
    auto *app = (DataBrowserApp *)t->user_data;
    app->try_start_fetch();
}
#endif

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool DataBrowserApp::run()
{
    ESP_LOGI(TAG, "DataBrowserApp::run()");

    lv_obj_t *screen = lv_scr_act();
    build_ui(screen);

#ifdef SIMULATOR_BUILD
    load_stub_paths();
    _fetch_done = true;
    on_fetch_complete(this);
#else
    // Try to fetch immediately, or wait for server discovery
    try_start_fetch();
#endif

    return true;
}

bool DataBrowserApp::back()
{
    if (_showing_detail) {
        show_list_view();
        return true;
    }
#ifndef SIMULATOR_BUILD
    if (_wait_timer) {
        lv_timer_del(_wait_timer);
        _wait_timer = nullptr;
    }
#endif
    return notifyCoreClosed();
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void DataBrowserApp::on_fetch_complete(void *user_data)
{
    auto *app = (DataBrowserApp *)user_data;
    if (app->_path_count > 0) {
        lv_obj_add_flag(app->_status_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(app->_path_list, LV_OBJ_FLAG_HIDDEN);
        app->populate_list();
    } else {
        lv_label_set_text(app->_status_lbl, "No paths found");
    }
}

void DataBrowserApp::on_path_click(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_current_target(e);
    auto *app = (DataBrowserApp *)lv_event_get_user_data(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(row);
    app->show_detail_view(idx);
}

void DataBrowserApp::on_back_click(lv_event_t *e)
{
    auto *app = (DataBrowserApp *)lv_event_get_user_data(e);
    app->show_list_view();
}

void DataBrowserApp::on_value_changed(void *user_data)
{
    auto *app = (DataBrowserApp *)user_data;
    if (!app->_showing_detail) return;

    static InstrumentStore d;
    if (!signalk_client_get_data(&d)) return;

    const PathValue *pv = store_find(d, app->_selected_path);

    char val_buf[32];
    char unit_buf[16];
    app->format_value(pv, app->_selected_units, val_buf, sizeof(val_buf),
                      unit_buf, sizeof(unit_buf));

    // Pick font size based on character count
    LV_FONT_DECLARE(lv_font_montserrat_120);  // 210px — 4+ chars
    LV_FONT_DECLARE(lv_font_montserrat_310);  // 310px — 3 chars
    LV_FONT_DECLARE(lv_font_montserrat_360);  // 360px — 1-2 chars
    int len = (int)strlen(val_buf);
    const lv_font_t *val_font;
    if (len <= 2)      val_font = &lv_font_montserrat_360;
    else if (len <= 3) val_font = &lv_font_montserrat_310;
    else               val_font = &lv_font_montserrat_120;

    lv_obj_set_style_text_font(app->_detail_value, val_font, 0);
    lv_label_set_text(app->_detail_value, val_buf);
    lv_obj_set_style_text_color(app->_detail_value,
        (pv && pv->valid) ? lv_color_white() : lv_color_hex(0x555555), 0);

    // Re-center value after text/font change
    lv_obj_align(app->_detail_value, LV_ALIGN_CENTER, 0, 0);
    lv_obj_update_layout(app->_detail_value);

    // Position unit anchored to value
    LV_FONT_DECLARE(lv_font_montserrat_120);  // actually 210px
    lv_label_set_text(app->_detail_unit, unit_buf);
    if (unit_buf[0]) {
        bool is_degree = (strcmp(unit_buf, "°") == 0);

        // Degree at 1/3 of value size, other units at 48px
        LV_FONT_DECLARE(lv_font_montserrat_90);
        LV_FONT_DECLARE(lv_font_montserrat_120);
        const lv_font_t *vf = lv_obj_get_style_text_font(app->_detail_value, 0);
        const lv_font_t *unit_font = &lv_font_montserrat_48;
        if (is_degree) {
            if (vf == &lv_font_montserrat_360)      unit_font = &lv_font_montserrat_120;  // 360/3=120
            else if (vf == &lv_font_montserrat_310)  unit_font = &lv_font_montserrat_90;   // 310/3≈103
            else                                     unit_font = &lv_font_montserrat_48;
        }
        lv_obj_set_style_text_font(app->_detail_unit, unit_font, 0);

        lv_obj_update_layout(app->_detail_unit);

        lv_coord_t vx = lv_obj_get_x(app->_detail_value);
        lv_coord_t vy = lv_obj_get_y(app->_detail_value);
        lv_coord_t vw = lv_obj_get_width(app->_detail_value);
        lv_coord_t vh = lv_obj_get_height(app->_detail_value);

        bool is_superscript = is_degree ||
                              strcmp(unit_buf, "%") == 0 ||
                              strcmp(unit_buf, "°C") == 0 ||
                              strcmp(unit_buf, "°F") == 0;

        if (is_degree) {
            lv_obj_set_pos(app->_detail_unit, vx + vw - 10, vy - 30);
        } else if (is_superscript) {
            lv_obj_set_pos(app->_detail_unit, vx + vw + 2, vy);
        } else {
            lv_obj_set_pos(app->_detail_unit, vx + vw + 4, vy + vh - 52);
        }
        lv_obj_clear_flag(app->_detail_unit, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(app->_detail_unit, LV_OBJ_FLAG_HIDDEN);
    }
}
