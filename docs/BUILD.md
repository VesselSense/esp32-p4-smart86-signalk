# Build System Reference

This document covers the component dependency structure, sdkconfig settings,
and the IDF 5.4-specific issues that had to be resolved before this project
built successfully. It exists so these problems are not rediscovered from scratch.

---

## Toolchain

| Item | Value |
|------|-------|
| Framework | ESP-IDF v5.4 |
| Target | esp32p4 |
| Toolchain | riscv32-esp-elf-gcc 14.2.0 |
| Python venv | `~/.espressif/python_env/idf5.4_py3.14_env` |
| Build command | `get_idf && idf.py build` |
| Binary output | `build/waveshare_p4_signalk.bin` (~2MB) |

`get_idf` is an alias defined in `~/.zshrc`:
```sh
alias get_idf='. $HOME/esp/esp-idf/export.sh'
```

---

## Component Dependencies

Dependencies are declared in `main/idf_component.yml` and resolved by the IDF
Component Manager on first build. They are downloaded to `managed_components/`
(gitignored).

```yaml
dependencies:
  idf: ">=5.4"
  waveshare/esp32_p4_wifi6_touch_lcd_4b: "^1.0.1"   # BSP: display, touch, LVGL port
  espressif/esp-ui: "0.2.*"                           # phone launcher shell
  espressif/esp_websocket_client: "*"                 # WS client (not bundled in IDF 5.x)
```

Resolved versions (from `dependencies.lock`):

| Component | Version | Notes |
|-----------|---------|-------|
| `espressif/esp_websocket_client` | 1.6.1 | Moved out of IDF core |
| `espressif/esp-ui` | 0.2.1 | Phone launcher shell |
| `lvgl/lvgl` | 8.4.0 | Pulled in by BSP |
| `espressif/esp_lvgl_port` | 2.7.2 | Pulled in by BSP |
| `waveshare/esp32_p4_wifi6_touch_lcd_4b` | 1.0.1 | Board BSP |
| `waveshare/esp_lcd_st7703` | 1.0.5 | Display driver |
| `espressif/esp_lcd_touch_gt911` | 1.2.0 | Touch driver |

### Custom component REQUIRES

Each custom component lists its direct IDF/managed component dependencies in
its `CMakeLists.txt`:

**`components/signalk_client/CMakeLists.txt`**
```cmake
REQUIRES
    esp_websocket_client   # managed component (name as used in CMake)
    json                   # IDF's cJSON
    freertos
    esp_timer
    log
```
`esp_tls` is intentionally absent — it is a transitive dependency of
`esp_websocket_client` and does not need to be listed separately.

**`components/instrument_ui/CMakeLists.txt`**
```cmake
REQUIRES
    signalk_client
    espressif__esp-ui      # double-underscore: managed component name in CMake
    log
    esp_timer
```
The CMake name for managed components replaces `/` with `__` and `-` with `-`,
so `espressif/esp-ui` becomes `espressif__esp-ui`.

---

## sdkconfig.defaults

`sdkconfig.defaults` provides compile-time defaults. The generated `sdkconfig`
is gitignored. If `sdkconfig` is deleted it is regenerated from defaults on the
next build.

### Key settings and why they are required

**`CONFIG_IDF_TARGET="esp32p4"`**
Sets the target chip. Also inferred from `sdkconfig.defaults` by the build
system when no `sdkconfig` exists.

**`CONFIG_ESP_HOST_WIFI_ENABLED=y`**
The ESP32-P4 does not have an integrated WiFi radio. WiFi is provided by the
ESP32-C6 coprocessor via SDIO (esp-hosted). The P4 SoC is flagged
`SOC_WIRELESS_HOST_SUPPORTED` not `SOC_WIFI_SUPPORTED`. Without this setting
the WiFi Kconfig menu is not fully evaluated and `WIFI_INIT_CONFIG_DEFAULT()`
in `main.cpp` fails to compile because it references
`CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM` which only appears in the evaluated
Kconfig output when host WiFi is enabled.

**`CONFIG_LV_FONT_MONTSERRAT_28=y`**
Required by `wind_gauge_widget.cpp` for the centre AWA value label. This font
is not enabled by the BSP or esp-ui defaults.

**`CONFIG_SPIRAM_MODE_OCT=y` / `CONFIG_SPIRAM_SPEED_80M=y`**
These were present in the initial `sdkconfig.defaults` but are flagged as
unknown by the build system (`warning: unknown kconfig symbol`). They are
harmless — the BSP's own Kconfig handles PSRAM configuration internally. They
can be removed in a future cleanup.

**`CONFIG_ESP_TLS_INSECURE=y` / `CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y`**
Allows the WebSocket client to connect to a dev server without verifying the
TLS certificate. Required if using ngrok or a self-signed cert. Remove or
guard with `#if` for a production build.

---

## Issues Resolved During First Build

The project had never been compiled against the real ESP-IDF toolchain. Six
build errors were encountered and fixed in sequence.

---

### 1. Python venv built with wrong interpreter

**Symptom:**
```
IDF_PYTHON_ENV_PATH: ~/.espressif/python_env/idf5.4_py3.9_env
ERROR: Activation script failed
```

**Root cause:**
macOS Xcode Python 3.9.6 is at `/usr/bin/python3` and appears early in
`PATH`. IDF's `install.sh` picked it up and created a 3.9 venv.
IDF 5.4's dependency checker uses `importlib.metadata` APIs that require
Python ≥ 3.10. The activation check always fails with 3.9.

A 3.13 venv (`idf5.4_py3.13_env`) had already been created from a previous
install attempt but was never activated because `export.sh` still selected the
3.9 venv when `python3` resolved to 3.9.

**Fix:**
Prepend Homebrew to PATH so `export.sh` selects the 3.13 Python and
activates the 3.13 venv:

```sh
export PATH="/opt/homebrew/bin:$PATH"
source ~/esp/esp-idf/export.sh
```

Added permanently to `~/.zshrc`.

---

### 2. `esp_websocket_client` component not found

**Symptom:**
```
CMake Error: Failed to resolve component 'esp_websocket_client' required by
component 'signalk_client': unknown name.
```

**Root cause:**
`esp_websocket_client` was bundled inside ESP-IDF in earlier versions. In
IDF 5.x it was extracted to the IDF Component Registry as
`espressif/esp_websocket_client`. The `signalk_client` CMakeLists.txt listed
it as a REQUIRES dependency as if it were still an IDF built-in.

**Fix:**
Added to `main/idf_component.yml`:
```yaml
espressif/esp_websocket_client: "*"
```
Resolved to version 1.6.1. The component name in CMake REQUIRES stays
`esp_websocket_client` (IDF maps managed components by their short name).

---

### 3. `esp_tls` component not found

**Symptom:**
```
CMake Error: Failed to resolve component 'esp_tls' required by component
'signalk_client': unknown name.
```

**Root cause:**
In IDF 5.4 the TLS component directory is named `esp-tls` (hyphen). The
REQUIRES list had `esp_tls` (underscore). More importantly, `signalk_client`
does not call any `esp_tls` APIs directly — TLS is handled internally by
`esp_websocket_client`, which declares `esp-tls` as its own dependency.

**Fix:**
Removed `esp_tls` from `signalk_client`'s REQUIRES list entirely.

---

### 4. `esp_ui.hpp` not found in `instrument_ui`

**Symptom:**
```
fatal error: esp_ui.hpp: No such file or directory
    3 | #include "esp_ui.hpp"
```

**Root cause:**
`instrument_helpers.hpp` (and `wind_gauge_widget.hpp`) include `esp_ui.hpp`
from the `espressif/esp-ui` managed component. The `instrument_ui` component's
CMakeLists.txt did not declare `espressif__esp-ui` as a dependency, so the
include path was not on the compiler's search list.

**Fix:**
Added to `instrument_ui/CMakeLists.txt` REQUIRES:
```cmake
espressif__esp-ui
```
Note: the CMake name for managed components uses double-underscore for the
namespace separator (`espressif/esp-ui` → `espressif__esp-ui`).

---

### 5. `esp_ui_phone_app_launcher_image_default` undeclared

**Symptom:**
```
error: 'esp_ui_phone_app_launcher_image_default' was not declared in this scope
   16 |                       &esp_ui_phone_app_launcher_image_default,
```

**Root cause (symbol declaration):**
The symbol is an LVGL image descriptor defined in
`managed_components/espressif__esp-ui/src/systems/phone/assets/esp_ui_phone_app_launcher_image_default.c`.
LVGL image descriptors must be forward-declared with `LV_IMG_DECLARE()` in
any translation unit that references them. The app constructors used the symbol
without declaring it.

**Fix — forward declaration:**
Added to the top of both `signalk_app.cpp` and `wind_gauge_app.cpp`,
before the constructor:
```cpp
LV_IMG_DECLARE(esp_ui_phone_app_launcher_image_default);
```

**Root cause (wrong constructor arity):**
`ESP_UI_PhoneApp` has a 5-argument convenience constructor:
```cpp
ESP_UI_PhoneApp(const char *name,
                const void *launcher_icon,
                bool use_default_screen,
                bool use_status_bar,
                bool use_navigation_bar);
```
Both app constructors were calling it with only 4 arguments, omitting
`use_default_screen`. The comments in the original code labelled the 3rd and
4th arguments as `use_status_bar` and `use_navigation_bar`, which were actually
the 4th and 5th parameters.

**Fix — correct arity:**
```cpp
// Before (4 args, wrong):
ESP_UI_PhoneApp("SignalK", &esp_ui_phone_app_launcher_image_default,
                true,   // use_status_bar   ← wrong
                true)   // use_navigation_bar

// After (5 args, correct):
ESP_UI_PhoneApp("SignalK", &esp_ui_phone_app_launcher_image_default,
                true,   // use_default_screen
                true,   // use_status_bar
                true)   // use_navigation_bar
```

---

### 6. `lv_font_montserrat_28` undeclared

**Symptom:**
```
error: 'lv_font_montserrat_28' was not declared in this scope; did you mean
'lv_font_montserrat_48'?
```

**Root cause:**
`wind_gauge_widget.cpp` uses `lv_font_montserrat_28` for the centre AWA
value label. `sdkconfig.defaults` only enabled Montserrat 14, 24, 36, and 48.
The 28-point variant was never listed.

In the simulator this was not caught because `simulator/lv_conf.h` had
`LV_FONT_MONTSERRAT_28=1` enabled.

**Fix:**
Added to `sdkconfig.defaults`:
```
CONFIG_LV_FONT_MONTSERRAT_28=y
```
Also deleted `sdkconfig` (the cached generated file) so the new default took
effect on the next build.

---

### 7. `CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM` undeclared

**Symptom:**
```
error: 'CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM' was not declared in this scope
   53 |     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
```

**Root cause:**
`WIFI_INIT_CONFIG_DEFAULT()` is a C macro in `esp_wifi.h` that expands to a
struct initialiser containing `.espnow_max_encrypt_num = CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM`.
This `CONFIG_` symbol is generated from the WiFi Kconfig only when the WiFi
subsystem is evaluated.

The ESP32-P4 has `SOC_WIRELESS_HOST_SUPPORTED` instead of `SOC_WIFI_SUPPORTED`.
The IDF WiFi Kconfig menu is `visible if (SOC_WIFI_SUPPORTED || SOC_WIRELESS_HOST_SUPPORTED)`
but the inner settings (including ESPNOW) are gated on
`if (ESP_WIFI_ENABLED || ESP_HOST_WIFI_ENABLED)`. Neither flag was set, so
the WiFi Kconfig was never evaluated and the CONFIG symbol was never generated.

**Fix:**
Added to `sdkconfig.defaults`:
```
CONFIG_ESP_HOST_WIFI_ENABLED=y
```
This gates in the full WiFi Kconfig evaluation for host-mode WiFi, which
generates `CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM` and all other WiFi
CONFIG symbols required by `WIFI_INIT_CONFIG_DEFAULT()`.

---

## Local Toolchain Setup (macOS ARM64)

Installed 2026-03-23 on macOS Darwin 24.6.0 (Apple Silicon).

### Prerequisites

- **Homebrew**: `cmake` must be installed (`brew install cmake`)
- **Python**: 3.14+ via Homebrew (system Xcode Python 3.9 is too old — see Issue #1 above)

### ESP-IDF Installation

```sh
# Shallow clone (full clone with --recursive takes 30+ min on slow connections)
mkdir -p ~/esp && cd ~/esp
git clone --depth 1 -b v5.4 https://github.com/espressif/esp-idf.git

# Install toolchain for ESP32-P4 only (fetches ~4 tools, not all targets)
cd esp-idf && ./install.sh esp32p4

# Add shell alias
echo 'alias get_idf=". $HOME/esp/esp-idf/export.sh"' >> ~/.zshrc
```

### Installed tools (in `~/.espressif/tools/`)

| Tool | Version |
|------|---------|
| riscv32-esp-elf-gdb | 14.2_20240403 |
| riscv32-esp-elf | esp-14.2.0_20241119 |
| openocd-esp32 | v0.12.0-esp32-20241016 |
| esp-rom-elfs | 20241011 |

Python venv: `~/.espressif/python_env/idf5.4_py3.14_env`

### Changing Network Configuration

If you change WiFi SSID or SignalK server IP in `sdkconfig.defaults`, delete `sdkconfig`
before rebuilding so defaults are regenerated:
```sh
get_idf && rm -f sdkconfig && idf.py build
```

---

## Build Reproducibility Notes

- `sdkconfig` is gitignored. It is regenerated from `sdkconfig.defaults` on
  each fresh build. Do not commit it.
- `managed_components/` is gitignored. Components are re-downloaded by the
  component manager. `dependencies.lock` is committed and pins exact versions.
- Deleting `build/` and `sdkconfig` gives a fully clean state. The next
  `idf.py build` re-runs cmake, re-downloads nothing (managed_components cache
  is warm), and rebuilds everything from source.
- The two SPIRAM Kconfig symbols (`SPIRAM_MODE_OCT`, `SPIRAM_SPEED_80M`) emit
  `unknown kconfig symbol` warnings during cmake. They are harmless — the BSP
  handles PSRAM config internally — but can be removed from `sdkconfig.defaults`
  in a future cleanup.
