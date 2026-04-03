#pragma once

#include "esp_ui.hpp"
#include "lvgl.h"
#include "signalk_client.hpp"

struct cJSON;

class DataBrowserApp : public ESP_UI_PhoneApp {
public:
    DataBrowserApp();
    ~DataBrowserApp() override;

    bool run()    override;
    bool back()   override;
    bool pause()  override { return true; }
    bool resume() override { return true; }

protected:
    // --- Views ---
    lv_obj_t *_list_view   = nullptr;
    lv_obj_t *_detail_view = nullptr;
    bool _showing_detail   = false;

    // List view widgets
    lv_obj_t *_path_list   = nullptr;
    lv_obj_t *_status_lbl  = nullptr;   // "Loading..." or path count

    // Detail view widgets
    lv_obj_t *_detail_name  = nullptr;
    lv_obj_t *_detail_path  = nullptr;
    lv_obj_t *_detail_value = nullptr;
    lv_obj_t *_detail_unit  = nullptr;

    char _selected_path[96] = {};
    char _selected_units[16] = {};

    // --- Path data ---
    struct PathEntry {
        char path[96];
        char display_name[64];
        char group[32];
        char units[16];
        bool is_numeric;
    };
    static const int MAX_PATHS = 128;
    PathEntry _paths[MAX_PATHS];
    int _path_count = 0;
    volatile bool _fetch_done = false;

    // --- Methods ---
    void build_ui(lv_obj_t *screen);
    void show_list_view();
    void show_detail_view(int path_idx);
    void populate_list();
    void format_value(const PathValue *pv, const char *units, char *buf, int buf_len,
                      char *unit_buf, int unit_len);

#ifdef SIMULATOR_BUILD
    void load_stub_paths();
#else
    static void fetch_task(void *arg);
    void walk_json(cJSON *obj, char *prefix, int prefix_len);
#endif

    lv_timer_t *_wait_timer = nullptr;

    static void extract_group(const char *path, char *out, int out_len);
    static int path_compare(const void *a, const void *b);

    // Callbacks
    void try_start_fetch();
    static void on_wait_timer(lv_timer_t *t);
    static void on_path_click(lv_event_t *e);
    static void on_back_click(lv_event_t *e);
    static void on_value_changed(void *user_data);
    static void on_fetch_complete(void *user_data);
};
