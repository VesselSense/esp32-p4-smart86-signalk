# LVGL Simulator — UI Validation Without Hardware

The simulator compiles `signalk_app.cpp` (the actual firmware UI code) as a native
Mac binary using the same LVGL 8.x library as the firmware. It renders each
instrument panel page to a 720×720 PNG — the same resolution as the physical display.

This is the primary validation mechanism for agentic UI development. No hardware,
no flash cycle, no visual inspection required.

---

## Location

```
simulator/
  CMakeLists.txt        — CMake build, fetches LVGL 8.4.0 and stb via FetchContent
  lv_conf.h             — LVGL configuration matching sdkconfig.defaults (fonts, widgets)
  main.cpp              — Headless renderer: LVGL init, flush_cb, PNG save, page scrolling
  render.sh             — One-command build + render script
  stubs/                — Desktop shims for ESP-IDF and esp-ui dependencies
    esp_log.h           — ESP_LOGI/LOGD/LOGE → printf
    esp_timer.h         — esp_timer_get_time() → clock_gettime(CLOCK_MONOTONIC)
    sdkconfig.h         — CONFIG_DISPLAY_UPDATE_MS=1, CONFIG_DATA_STALE_MS=5000
    esp_ui.hpp          — Minimal ESP_UI_PhoneApp stub (getVisualArea, notifyCoreClosed)
    esp_ui_stub.cpp     — esp_ui_phone_app_launcher_image_default dummy
    signalk_client_stub.cpp — Hardcoded sample values in SI units
    freertos/
      FreeRTOS.h        — typedef void* SemaphoreHandle_t
      semphr.h          — (empty)
  build/                — CMake build artifacts (gitignored)
    lvgl_sim            — Executable
  screenshots/          — Output PNGs (gitignored, always at this known path)
    page1.png .. page4.png
```

---

## First-Time Setup (downloads LVGL ~30s)

Run from the project root:

```sh
cmake -B simulator/build -S simulator -DCMAKE_BUILD_TYPE=Release -Wno-dev
```

---

## Render

All pages:
```sh
simulator/render.sh
```

Single page:
```sh
simulator/render.sh 3
```

Autopilot app:
```sh
simulator/render.sh autopilot
```

Wind Rose app:
```sh
simulator/render.sh windrose
```

Output lands in `simulator/screenshots/pageN.png`, `autopilot.png`, or `windrose.png` regardless of working directory.

---

## Agentic Validation Loop

```
edit components/signalk_app/signalk_app.cpp (or signalk_app.hpp)
  → simulator/render.sh [page]
  → Read tool reads simulator/screenshots/pageN.png
  → verify layout, colors, font sizes, alignment
  → iterate
```

The Read tool supports PNG images — screenshots are read directly and visually
verified without any additional tooling.

---

## Architecture

The simulator compiles the **actual firmware source files** directly:

| File | Role |
|------|------|
| `components/signalk_app/signalk_app.cpp` | SignalK instrument UI — compiled as-is |
| `components/autopilot_app/autopilot_app.cpp` | Autopilot UI — compiled as-is |
| `components/autopilot_app/lv_font_roboto_96.c` | Custom 192px Roboto Bold font |
| `components/wind_rose_app/wind_rose_app.cpp` | Wind Rose UI — compiled as-is |
| `components/wind_rose_app/lv_font_montserrat_76.c` | Custom 76px Montserrat Bold font |
| `simulator/stubs/signalk_client_stub.cpp` | Replaces real WebSocket client with hardcoded data |
| `simulator/stubs/esp_ui.hpp` | Replaces esp-brookesia's `ESP_UI_PhoneApp` base class |
| `simulator/stubs/esp_ui_stub.cpp` | Provides the dummy launcher icon symbol |
| `simulator/main.cpp` | LVGL init, flush_cb → raw RGBA buffer, PNG save via stb |

`SimulatorApp` / `SimulatorAutopilotApp` / `SimulatorWindRoseApp` in `main.cpp` are thin
subclasses that expose the `protected run()` method. They call `run()` directly, bypassing
the esp-ui phone shell.

`getVisualArea()` in the stub returns `{0, 0, 719, 719}` (full 720×720) since there
is no status bar or navigation bar in the simulator.

---

## Design Constraints

- Display is 720×720 px
- `lv_conf.h` enables:
  - Fonts: Montserrat 14, 28, 36, 48 + `LV_FONT_FMT_TXT_LARGE` for custom large fonts
  - Widgets: `lv_arc`, `lv_bar`, `lv_label`, `lv_line`, `lv_img`, `lv_meter`
  - Layout: flex (row/column/wrap)
- Colors, fonts, and layout come directly from the app source files — no duplication
- Stub data is in SI units matching SignalK conventions (m/s, radians, Kelvin, 0–1 ratios)
- To use a new LVGL widget, enable it in **both** `simulator/lv_conf.h` (simulator) and `sdkconfig.defaults` or `managed_components/.../lv_conf.h` (firmware)

---

## Adding a New Instrument

1. Add the instrument to `signalk_app.cpp` / `signalk_app.hpp` following the existing pattern
2. Add a representative stub value (SI units) to `simulator/stubs/signalk_client_stub.cpp`
3. Run `simulator/render.sh` to validate

No changes to `simulator/main.cpp` are needed for new instruments or pages.

---

## Known Simulator Limitations

- The esp-ui phone launcher home screen is not simulated (no app icon, no launcher chrome)
- `getVisualArea()` returns full 720×720 — on device the status/nav bars reduce this
- Touch and gesture interaction cannot be tested (static render only)
