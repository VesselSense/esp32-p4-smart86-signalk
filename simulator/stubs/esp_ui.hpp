#pragma once
#include "lvgl.h"

/*
 * Minimal stub for esp-brookesia's ESP_UI_PhoneApp.
 * Provides just enough interface for signalk_app.cpp to compile and run
 * without the esp-ui phone launcher shell.
 *
 * getVisualArea() returns the full 720x720 display — no status/nav bar
 * deduction since there is no shell in the simulator.
 */

/* Dummy app icon image referenced in SignalKApp constructor */
extern const lv_img_dsc_t esp_ui_phone_app_launcher_image_default;

class ESP_UI_PhoneApp {
public:
    ESP_UI_PhoneApp(const char * /*name*/,
                    const lv_img_dsc_t * /*icon*/,
                    bool /*use_default_screen*/,
                    bool /*use_status_bar*/,
                    bool /*use_navigation_bar*/) {}

    virtual ~ESP_UI_PhoneApp() {}

    virtual bool run()    = 0;
    virtual bool back()   = 0;
    virtual bool pause()  = 0;
    virtual bool resume() = 0;

protected:
    /* Called by back() in SignalKApp — no-op in simulator */
    bool notifyCoreClosed() { return true; }

    /* Returns the usable screen area. Simulator uses full display. */
    lv_area_t getVisualArea()
    {
        lv_area_t area = {0, 0, 719, 719};
        return area;
    }
};
