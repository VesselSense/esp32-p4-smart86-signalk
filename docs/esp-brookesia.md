# esp-brookesia v0.2.0 API Reference

## Repository
- GitHub: https://github.com/espressif/esp-brookesia (tag: v0.2.0)
- Note: in code the library is named `esp-ui`; class prefix is `ESP_UI_`
- Latest release: v0.4.2 (Dec 2024) — v0.2.x uses LVGL 8.x, v0.3+ uses LVGL 9.x

## Core Classes

### ESP_UI_Phone (phone shell / launcher)
```cpp
#include "esp_ui.hpp"

ESP_UI_Phone *phone = new ESP_UI_Phone(lv_disp_t *display = nullptr);
phone->setTouchDevice(input_dev);
phone->begin();
phone->installApp(app_ptr);  // or ref
phone->uninstallApp(app_ptr);
phone->getHome()             // ESP_UI_PhoneHome&
phone->getManager()          // ESP_UI_PhoneManager&
```

### ESP_UI_PhoneApp (base class for apps)
```cpp
class MyApp : public ESP_UI_PhoneApp {
public:
    // Simple constructor:
    MyApp(bool use_status_bar = true, bool use_navigation_bar = true);
    // Internally calls:
    // ESP_UI_PhoneApp(name, icon_ptr, use_default_screen, use_status_bar, use_nav_bar)

protected:
    bool run() override;    // REQUIRED: create all LVGL UI here
    bool back() override;   // REQUIRED: call notifyCoreClosed() to exit
    bool close() override;  // optional
    bool pause() override;  // optional
    bool resume() override; // optional
    bool cleanResource() override; // optional - for non-tracked resources
};
```

### run() notes
- Framework creates a default screen; use `lv_scr_act()` to get it
- All `lv_obj_create(NULL)`, `lv_timer_create()`, `lv_anim_start()` calls are auto-tracked
- Resources auto-cleaned on close if `enable_recycle_resource` is set
- Use `getVisualArea()` to get the visible rect (excludes status/nav bars)

## P4 main.cpp Pattern (from official example)
```cpp
#include "bsp/esp-bsp.h"
#include "esp_ui.hpp"

extern "C" void app_main(void) {
    bsp_i2c_init();
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .flags = { .buff_spiram = true }
    };
    lv_disp_t *disp = bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    bsp_display_lock(0);

    ESP_UI_Phone *phone = new ESP_UI_Phone(disp);
    phone->setTouchDevice(bsp_display_get_input_dev());
    phone->begin();
    phone->installApp(new MyApp(true, true));

    lv_timer_create(clock_cb, 1000, phone);  // example periodic timer

    bsp_display_unlock();
    // main task can monitor memory or vTaskDelay forever
}
```

## idf_component.yml (from P4 example)
```yaml
dependencies:
  idf: ">=5.3"
  espressif/esp32_p4_function_ev_board: "^3"  # replace with Waveshare BSP
  espressif/esp-ui:
    version: "*"
    # For v0.2.0: version: "0.2.0"
```

## App Icon
The default icon is `esp_ui_phone_app_launcher_image_default` (declared in esp_ui.hpp).
For custom icons: declare as `LV_IMG_DECLARE(my_icon)` and pass `&my_icon`.

## LVGL Thread Safety
Always wrap LVGL calls from non-main-task context:
```cpp
bsp_display_lock(0);
lv_label_set_text(label, buf);
bsp_display_unlock();
```
Or use `lv_async_call()` for callbacks.

## Scrollable instruments pattern
Use `lv_obj_t *container` with `lv_obj_set_scroll_dir(container, LV_DIR_VER)`
Each instrument is a child `lv_obj_t` panel with fixed height.
Swipe scrolls between instruments naturally via LVGL scroll.
