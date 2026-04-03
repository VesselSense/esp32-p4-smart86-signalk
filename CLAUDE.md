# CLAUDE.md — waveshare-p4

SignalK instrument panel for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B ("Smart 86 Box").
Prototype of a 4" touchscreen SignalK display.

## Hardware

- **Board**: Waveshare ESP32-P4-WIFI6-Touch-LCD-4B
- **Display**: 4" IPS 720×720 MIPI-DSI, ST7703 driver
- **Touch**: GT911 capacitive (I2C: SDA=GPIO7, SCL=GPIO8)
- **WiFi**: ESP32-C6 coprocessor via SDIO (esp-hosted; C6 ships pre-flashed by Waveshare)
- **Flash**: 16MB · **PSRAM**: 8MB OSPI

## Toolchain

- **Framework**: ESP-IDF v5.4
- **Build**: `get_idf && idf.py build` (`get_idf` alias sources `~/esp/esp-idf/export.sh`)
- **Flash**: `idf.py -p /dev/cu.usbmodem* flash` (monitor requires TTY — use separate terminal)
- **Config**: All user settings (WiFi, timezone, units) managed on-device via Settings and WiFi apps (NVS-persisted). `idf.py menuconfig` only for mDNS timeout and display intervals
- **Makefile**: `make build`, `make flash PORT=...`, `make capture PORT=...` (see below)
- Dependencies declared in `main/idf_component.yml`, auto-fetched into `managed_components/` (gitignored)
- See `docs/BUILD.md` for component dependency details and IDF 5.4 build issues resolved

## Serial Debug

**Monitor requires a TTY** — `idf.py monitor` doesn't work in Claude Code's bash.
Use the capture script instead:

```sh
# Capture 20s of serial output (uses IDF's python env with pyserial)
make capture PORT=/dev/cu.usbmodem*

# Or directly:
zsh -c 'source $HOME/esp/esp-idf/export.sh 2>/dev/null && \
  python3 scripts/capture_serial.py /dev/cu.usbmodem* 20'
```

- Output saved to `/tmp/serial_capture.txt` and printed to stdout
- Script: `scripts/capture_serial.py` (requires pyserial from IDF python env)
- Port varies — use `ls /dev/cu.usbmodem*` to find it
- Default duration: 20 seconds (pass as second arg to override)

## UI Validation (Simulator)

**Primary method for agentic UI development** — no hardware required.

```sh
# First time only — downloads LVGL (~30s)
cmake -B simulator/build -S simulator -DCMAKE_BUILD_TYPE=Release -Wno-dev

# Render all pages → simulator/screenshots/page{1..4}.png
simulator/render.sh

# Render one page
simulator/render.sh 3
```

- Renders 720×720 PNG via headless LVGL 8.x — same library as the firmware
- Output always at `simulator/screenshots/pageN.png` (stable known path)
- Read tool reads the PNG directly to verify layout, fonts, and colors
- See `docs/SIMULATOR.md` for full workflow

## UI Framework

**esp-ui (esp-brookesia) v0.2.x** — phone-style launcher shell built on LVGL 8.x.

- In code the library is `esp-ui`; class prefix is `ESP_UI_`
- `ESP_UI_Phone` — the launcher shell (home screen, app icons, gestures)
- `ESP_UI_PhoneApp` — base class for all apps
- Apps are registered at startup via `phone->installApp()` in `main/main.cpp`
- **No runtime app loading** — adding an app requires recompile + reflash
- See `docs/esp-brookesia.md` for full API reference

## Project Structure

```
CLAUDE.md                        ← you are here
CMakeLists.txt                   ← top-level ESP-IDF CMake, lists component dirs
platformio.ini                   ← PlatformIO config (board, framework, flash size)
sdkconfig.defaults               ← board-specific IDF config (PSRAM, fonts, WiFi, TLS)
partitions.csv                   ← 16MB flash layout (6MB app, SPIFFS remainder)

main/
  main.cpp                       ← entry point: BSP init, WiFi, mDNS discovery, launcher overlay, installApp()
  idf_component.yml              ← dependencies: Waveshare BSP, esp-ui, esp_wifi_remote, esp_hosted, esp_websocket_client, mdns
  Kconfig.projbuild              ← menuconfig items: mDNS timeout, update intervals
  CMakeLists.txt

components/
  signalk_client/                ← WebSocket + dynamic data store (runs in its own FreeRTOS task)
    signalk_client.hpp           ← InstrumentStore (flat PathValue array), connection state, public API
    signalk_client.cpp           ← esp_websocket_client, cJSON delta parser, observer (watch), subscribe/unsubscribe
    sk_logo_alpha.h              ← 120×85 alpha map of SignalK logo (generated from official PNG)
  data_browser_app/              ← Dynamic SignalK path browser (HTTP discovery + live values)
    data_browser_app.hpp         ← ESP_UI_PhoneApp subclass, two-view (list + detail)
    data_browser_app.cpp         ← HTTP path fetch, lv_list index, observer-driven value display
    lv_font_montserrat_*.c       ← custom fonts: 90px (degree), 210px (4+ chars), 310px (3 chars), 360px (1-2 chars)
  signalk_app/                   ← DEPRECATED — excluded from build, kept for reference
  wind_rose_app/                 ← Heading-relative compass rose with AWS/TWS + corner panels
    wind_rose_app.hpp            ← ESP_UI_PhoneApp subclass declaration
    wind_rose_app.cpp            ← rotating tick ring, cardinal labels, corner data panels
    lv_font_montserrat_76.c      ← custom 76px Montserrat Bold for AWS digits
    lv_font_montserrat_67.c      ← custom 67px Montserrat Bold for corner panel values
    lv_font_montserrat_bold_50.c ← custom 50px Montserrat Bold for TWS overlay
  signalk_auth/                  ← SignalK device auth + authenticated API calls + base URL
    signalk_auth.hpp             ← public API: init, request_access, validate, api_call, get_base_url
    signalk_auth.cpp             ← NVS token storage, UUID, HTTP POST/poll, FreeRTOS task
  wifi_manager/                  ← WiFi scan, connect, NVS credential persistence
    wifi_manager.hpp             ← public API: init, connect, scan, load/save/forget networks
    wifi_manager.cpp             ← esp_wifi wrapper, NVS storage, connection timeout, error mapping
  wifi_settings_app/             ← On-device WiFi configuration app (LVGL UI)
    wifi_settings_app.hpp        ← ESP_UI_PhoneApp subclass, connected/disconnected views
    wifi_settings_app.cpp        ← two-view UI, autocomplete, known networks, NVS save on connect
  unit_config/                   ← Standalone unit conversion + preferences (NVS-persisted)
    unit_config.hpp              ← enums, convert(), label(), option enumeration API
    unit_config.cpp              ← conversion tables, NVS load/save, timezone apply
  settings_app/                  ← Settings app with gear icon (first in launcher)
    settings_app.hpp             ← ESP_UI_PhoneApp subclass, generic picker pattern
    settings_app.cpp             ← timezone + unit category pickers, delegates to unit_config
    settings_gear_icon.c         ← 112×112 gear icon (generated from reference)
  audio_feedback/                ← Button press audio feedback (ES8311 codec via I2S)
    audio_feedback.hpp           ← public API: init, click, engage, disengage, invalid
    audio_feedback.cpp           ← pre-generated click, frequency sweeps, double buzz

fonts/
  Montserrat-Bold.ttf            ← source font for custom LVGL font generation (see docs/FONTS.md)

docs/
  SETUP.md                       ← prerequisites, build/flash workflow, adding instruments
  BUILD.md                       ← component deps, sdkconfig, IDF 5.4 build issues resolved
  DEV_SERVER.md                  ← local dev server start, curl verification, ESP32 connection
  SIMULATOR.md                   ← LVGL PC simulator workflow and architecture
  FONTS.md                       ← custom LVGL font generation with lv_font_conv
  esp-brookesia.md               ← esp-ui v0.2.0 API reference (researched)
  signalk-paths.md               ← example SignalK paths, units, conversions

simulator/
  render.sh                      ← one-command build + render (→ simulator/screenshots/)
  main.cpp                       ← headless LVGL renderer
  stubs/                         ← desktop shims for esp-ui, signalk_client, FreeRTOS, IDF
  screenshots/                   ← page1.png .. page4.png (output, gitignored)
```

## Architecture

```
app_main()
  │
  ├── bsp_i2c_init() + bsp_display_start_with_config()   ← Waveshare BSP
  ├── wifi_manager_init(on_wifi_got_ip)                    ← blocking scan at boot
  ├── unit_config_init()                                   ← load TZ + units from NVS (after NVS init)
  │     └── wifi_manager_connect_saved()                   ← best NVS network by RSSI
  │           └── on IP_EVENT_STA_GOT_IP (callback)
  │                 ├── discover_signalk_uri()              ← mDNS only, no fallback URI
  │                 ├── signalk_client_start(uri)           ← starts WS task
  │                 ├── signalk_auth_init(uri)              ← loads token from NVS
  │                 └── init_sntp()                         ← NTP time sync for status bar clock
  │
  └── ESP_UI_Phone::begin()
        ├── Status bar: WiFi icon + 12h clock (battery hidden)
        ├── installApp(new SettingsApp())        ← timezone + unit preferences
        ├── installApp(new DataBrowserApp())     ← dynamic SignalK path browser
        ├── installApp(new AutopilotApp())       ← autopilot controller
        ├── installApp(new WindRoseApp())        ← heading compass rose + AWS
        └── installApp(new WifiSettingsApp())    ← on-device WiFi configuration
              │
              └── *App::run()                             ← called when user taps icon
                    ├── build_ui()                         ← LVGL widget tree
                    └── observer or timer for data updates
```

## WiFi Manager

**Component**: `components/wifi_manager/` — centralised WiFi scan, connect, and NVS credential management.

**Boot flow**:
1. `wifi_manager_init()` — initialises WiFi subsystem + runs a **blocking** scan
2. `wifi_manager_connect_saved()` — loads saved networks from NVS, cross-references
   against scan results (sorted by RSSI), connects to strongest saved match
3. If no saved network in range → stays disconnected, user opens WiFi Settings app
4. On IP obtained → fires callback to `main.cpp` for mDNS discovery + SignalK connect

**NVS storage**: namespace `wifi_mgr`, up to 5 networks (`ssid0`/`pw0` .. `ssid4`/`pw4`).
Oldest entry evicted when full. Saved on successful connect, available across reboots.

**Connection timeout**: 10-second FreeRTOS timer. If no IP within 10s → `WIFI_MGR_CONNECT_FAILED`.

**Error mapping**: disconnect reason codes mapped to user messages (wrong password, network not
found, authentication failed, AP not responding, connection timed out).

**Guards**: `wifi_manager_connect()` rejects if scanning. `wifi_manager_scan_start()` rejects
if connecting. Both return `false` so caller can show "busy" feedback.

**Intentional disconnect handling**: when `connect()` tears down old connection before starting
new one, the disconnect event is suppressed via `s_intentional_disconnect` flag.

## WiFi Settings App

**Component**: `components/wifi_settings_app/` — on-device WiFi configuration with two views.

**Connected view** (shown when WiFi is connected):
- Current network name + signal strength (dBm)
- Disconnect button (red)
- List of other saved networks with Switch buttons

**Disconnected view** (shown when not connected):
- Known networks in range (saved + scanned match) with one-tap Connect buttons
- SSID text input with autocomplete (2+ chars, prefix match against scan results)
- Password text input (unmasked)
- On-screen keyboard (16px Montserrat)
- Status label (Connecting.../Connected/error message)

**Auto-scan**: scan runs at boot (`wifi_manager_init`) and on app open. Results persist
in wifi_manager static memory across app open/close.

**Credential save**: on successful connect, SSID+password saved to NVS automatically.

**View switching**: poll timer (500ms) monitors `wifi_manager_get_state()` and switches
between connected/disconnected views automatically on state change.

## Unit Config

**Component**: `components/unit_config/` — standalone unit conversion and preferences.
Any app includes `unit_config.hpp` and calls `unit_convert(category, si_value)` — no
dependency on the settings app.

**Categories**: `UNIT_CAT_TIMEZONE`, `UNIT_CAT_SPEED`, `UNIT_CAT_DEPTH`,
`UNIT_CAT_TEMPERATURE`, `UNIT_CAT_DISTANCE`, `UNIT_CAT_PRESSURE`, `UNIT_CAT_VOLUME`.

**API**:
- `unit_config_init()` — load all preferences from NVS, apply timezone (call once at boot)
- `unit_config_set(cat, idx)` — save to NVS immediately, apply timezone if applicable
- `unit_convert(cat, si_value)` — convert SI value to display unit
- `unit_label(cat)` / `unit_label_long(cat)` — get label string ("KT" / "kts")
- `unit_option_count(cat)` / `unit_option_name_at(cat, idx)` — for building picker UIs

**NVS**: namespace `unit_cfg`, keys `tz`, `spd`, `dep`, `tmp`, `dst`, `prs`, `vol`.

**IMPORTANT**: `unit_config_init()` must be called **after** `wifi_manager_init()` because
WiFi manager calls `nvs_flash_init()`. Calling it before NVS init causes all preferences
to silently default to 0.

## Settings App

**Component**: `components/settings_app/` — first app in launcher (gear icon).

**Main view**: 7 setting rows (Timezone, Speed, Depth, Temperature, Distance, Pressure,
Volume). Each shows current value and chevron. Tap to open picker.

**Generic picker**: single reusable view for all categories. Populates from
`unit_option_count()` / `unit_option_name_at()`. Checkmark on current selection.
Back button returns to main view. Selecting an option calls `unit_config_set()`.

**Timezone picker**: same generic picker, 23 timezone options. Selecting applies
`setenv("TZ",...) + tzset()` immediately via `unit_config_set(UNIT_CAT_TIMEZONE, idx)`.

## Status Bar

The esp-ui launcher home screen has a built-in status bar (always visible on launcher,
hidden when apps are open).

**WiFi icon**: updated every 500ms from `wifi_manager` state. Maps RSSI to 3 signal
levels (>-50 = full, >-70 = medium, else weak). Shows closed icon when disconnected.

**Clock**: SNTP syncs to `pool.ntp.org` after WiFi connects. Displays in 12-hour format
with AM/PM. Skips display until time is synced (year ≥ 2024). Timezone applied from
`unit_config` (NVS-persisted).

**Battery icon**: hidden — this board has no battery.

**Key file**: `main/main.cpp` → `update_status_bar_wifi()`, `update_status_bar_clock()`,
`init_sntp()`. All called from the 500ms `status_timer_cb`.

## Server Discovery (mDNS)

The device finds SignalK servers automatically — no hardcoded IP needed.

**How it works**: SignalK servers advertise `_signalk-ws._tcp` via mDNS (Bonjour/Zeroconf).
On WiFi connect, the firmware calls `mdns_query_ptr("_signalk-ws", "_tcp")`, extracts the
first IPv4 address and port from the result, and builds a WebSocket URI. **No fallback URI** —
if mDNS finds nothing, the device stays disconnected and retries via the status timer (500ms).

**Reconnect with re-discovery**: after 3 consecutive WebSocket failures, the client tears
down and re-runs mDNS discovery. If mDNS also fails on retry, the status timer continues
retrying until a server is found. WiFi reconnects also trigger a fresh mDNS query.

**Connection state** (`signalk_conn_state_t` in `signalk_client.hpp`):

| State | Meaning | Launcher icon |
|-------|---------|---------------|
| `SK_STATE_WIFI_CONNECTING` | Waiting for WiFi association + IP | Amber SK logo |
| `SK_STATE_MDNS_SEARCHING` | mDNS query in progress | Amber SK logo |
| `SK_STATE_WS_CONNECTING` | Server found, WebSocket handshake | Amber SK logo |
| `SK_STATE_CONNECTED` | WebSocket open, data flowing | Green SK logo |
| `SK_STATE_DISCONNECTED` | Connection lost, retrying | Red SK logo |

**Launcher overlay**: the SK logo (120×85 alpha map from official SignalK PNG, tinted by
state color) and status text are rendered on the esp-ui home screen via `lv_img` with
`LV_IMG_CF_ALPHA_8BIT` recoloring. Updated by a 500ms LVGL timer polling `signalk_client_get_state()`.

**Key files**: `discover_signalk_uri()` in `main/main.cpp`, `espressif/mdns` component in
`main/idf_component.yml`, state enum in `components/signalk_client/signalk_client.hpp`.

## Autopilot Auth & Control

**Auth module** (`components/signalk_auth/`): standalone component for device pairing and
authenticated API calls. Used by the autopilot app, available to any app.

**Pairing flow** (triggered by "Pair Device" button in autopilot menu):
1. Device generates persistent UUID (`clientId`) stored in NVS
2. POSTs to `/signalk/v1/access/requests` in a background FreeRTOS task
3. Polls `href` every 2s until admin approves via SignalK web UI
4. Stores JWT token in NVS — persists across reboots

**Token validation**: on autopilot app launch, validates token via `GET /signalk/v1/api/vessels/self`
(SignalK has no dedicated `/auth/validate` endpoint — validation uses a protected API endpoint).
Only clears token on explicit 401/403 rejection. Network errors or other status codes return
false but preserve the token to avoid losing credentials on transient failures.

**Autopilot control** uses SignalK v2 Autopilot API via `/state` endpoint (Raymarine maps
state to mode: `auto`=compass, `wind`=wind, `route`=track):

| Button | When | API Call |
|--------|------|---------|
| Auto | Standby → engaged | `PUT .../state {"value":"auto"/"wind"/"route"}` (based on mode) |
| STBY | Always | `PUT .../state {"value":"standby"}` |
| Mode | While engaged | `PUT .../state` (switches to new mode's state) |
| Mode | While standby | Local only — no API call |
| ±1°/±10° | While engaged | `PUT .../target/adjust {"value":N,"units":"deg"}` |

**Dev server setup**: requires `@signalk/signalk-autopilot` plugin in emulator mode
(`~/.signalk/plugin-config-data/autopilot.json` → `"type": "emulator"`).
Server must have security enabled (admin user created via web UI).
Use NMEA0183 sample data only (`--sample-nmea0183-data`) — N2K sample lacks `headingMagnetic`.

**Key files**: `signalk_auth.hpp/cpp`, `autopilot_app.cpp` → `on_btn_event()`, `docs/AUTOPILOT_UI.md`.

## Audio Feedback

**Hardware**: ES8311 codec, I2S interface, power amplifier on GPIO 53 (BSP-managed).

**Component**: `components/audio_feedback/` — initialized once at startup, codec stays open.

| Sound | Function | Usage |
|-------|----------|-------|
| Click (8ms noise burst) | `audio_feedback_click()` | Valid button press |
| Rising sweep 500→1200Hz, 1.5s | `audio_feedback_engage()` | Autopilot engaged |
| Falling sweep 1200→500Hz, 1.5s | `audio_feedback_disengage()` | Autopilot disengaged |
| Double low buzz 250Hz | `audio_feedback_invalid()` | Invalid/no-op button press |

Sweep volume at 10% amplitude, click at 100%.

## Autopilot Button State Logic

| Button | STBY state | AUTO state |
|--------|-----------|-----------|
| Mode | Click + cycle mode | Invalid buzz (ignored) |
| Auto | Sweep up + engage + overlay | Invalid buzz (already engaged) |
| STBY | Invalid buzz (already standby) | Sweep down + disengage + overlay |
| ±1/±10 | Invalid buzz (no action) | Click + adjust + API |
| Menu | Click + open menu | Click + open menu |

**Status overlay**: white rounded rect (500×200px) centered on screen, shows for 3 seconds
then disappears. Shows "Autopilot Engaged" + mode or "Standby" + "Autopilot disengaged".

## Data Flow

```
SignalK server (discovered via mDNS)
  → esp_websocket_client (FreeRTOS task)
  → parse_delta() via cJSON
  → InstrumentStore (flat PathValue array, mutex-protected)
  → [if TWS/TWA missing: derive from AWS + AWA + STW]
  → signalk_client_watch() observer → lv_async_call() to LVGL thread
  → OR signalk_client_get_data() snapshot for polling apps
```

**InstrumentStore** replaces the old fixed `InstrumentData` struct. It's a flat array of
64 `PathValue` entries keyed by SignalK path string. Any path can be stored at runtime.
`store_find(store, "path")` returns a pointer or nullptr. `store_set()` adds or updates.

**Observer pattern**: `signalk_client_watch(path, callback, user_data)` registers a callback
that fires on the LVGL thread (via `lv_async_call`) when the watched path receives new data.
Used by Data Browser — no polling timer needed.

**Polling pattern**: Autopilot and Wind Rose still use `signalk_client_get_data()` snapshot +
LVGL timer (500ms). The `static InstrumentStore d` avoids stack overflow (struct is ~6.4KB).

## UI Layout (720×720 — status bar on launcher, hidden in apps)

| App | Description |
|-----|-------------|
| **Settings** | Timezone + unit preferences (7 categories, generic picker pattern) |
| **Data Browser** | Dynamic SignalK path browser — HTTP discovery + live value display |
| **Autopilot** | Autopilot controller — compass, wind bar, engage/disengage |
| **Wind Rose** | Heading-relative compass rose + AWS/TWS + corner panels |
| **WiFi Settings** | On-device WiFi configuration (scan, connect, saved networks) |

**Heading fallback**: prefers `headingMagnetic`; falls back to `headingTrue` when magnetic unavailable.

**True wind derivation**: if server doesn't provide `wind.speedTrue`/`wind.angleTrueWater`,
client derives TWS/TWA from AWS + AWA + STW using the standard vector triangle.

**Connection status**: launcher home screen shows SK logo (green/amber/red) + text.
Per-app status dots (green/red) in SignalKApp and AutopilotApp via `signalk_client_is_connected()`.
Values dim to `CLR_STALE` when no update within `CONFIG_DATA_STALE_MS`.

## SignalK Server

**Dev server**: `../signalk-server/` — start with:
```sh
cd ../signalk-server && node bin/signalk-server --sample-nmea0183-data --sample-n2k-data --override-timestamps
```
Both `--sample-nmea0183-data` (plaka.log) and `--sample-n2k-data` (aava-n2k.data) are needed
for full path coverage. See `docs/DEV_SERVER.md` for details.

**Connection**: the device discovers the server automatically via mDNS (`_signalk-ws._tcp`).
The ESP32 and Mac must be on the same **2.4GHz** WiFi network (ESP32-C6 does not support 5GHz).
Set WiFi credentials via `idf.py menuconfig` → *SignalK Instrument Panel*.
No IP address configuration needed — mDNS handles it.

**Verify mDNS**: `dns-sd -B _signalk-ws._tcp` on Mac should show the server.

See `docs/signalk-paths.md` for example SignalK paths and unit conversions.
See `docs/DEV_SERVER.md` for the full connection workflow and which paths the dev server provides.

## Data Browser App

**Component**: `components/data_browser_app/` — dynamic SignalK path browser.

**Path discovery**: on app open, HTTP GET `/signalk/v1/api/vessels/self` fetches all paths.
Response parsed with cJSON tree walk. 256KB PSRAM buffer for large responses. Runs in
a background FreeRTOS task (16KB stack). If server not yet discovered (base URL unavailable),
polls every 500ms via LVGL timer until `signalk_auth_get_base_url()` returns non-null.

**Path naming**: uses `meta.displayName` from the server response if present, otherwise
shows the raw SignalK path. No hardcoded lookup table — all names come from the server.

**List view**: scrollable `lv_list` grouped by first path segment (Navigation, Environment,
Electrical, etc.). Paths sorted alphabetically within each group. Non-numeric paths
(strings, objects) shown dimmed and non-tappable.

**Detail view**: tap a numeric path → `signalk_client_subscribe()` + `signalk_client_watch()`
→ live value displayed in large font. Observer-driven (no polling timer) — `lv_async_call`
delivers updates from the WS task to the LVGL thread.

**Dynamic font sizing** based on character count:
- 1-2 chars: 360px Montserrat Bold (e.g. "66%")
- 3 chars: 310px (e.g. "215°")
- 4+ chars: 210px (e.g. "14.54 V")

**Unit positioning**: degree symbol (°) rendered at 1/3 of value font as superscript.
Text units (V, ft, kts) at 48px bottom-right. Percent (%) at 48px top-right.

**Value formatting** based on `meta.units` from the server:
- `m/s` → knots via `unit_convert(UNIT_CAT_SPEED)`
- `rad` → degrees (× 57.2958)
- `K` → °C/°F via `unit_convert(UNIT_CAT_TEMPERATURE)`
- `m` → ft via `unit_convert(UNIT_CAT_DEPTH)`
- `ratio` → percentage (× 100)
- Everything else → raw float with unit string

**Key design decisions**:
- No hardcoded path names — server provides `meta.displayName` or raw path is shown
- No WS unsubscribe message sent — caused server to drop connection. Paths stay in store.
- `InstrumentStore` is shared with autopilot/wind rose — initial 10 paths pre-populated,
  dynamic paths added on subscribe, never removed (prevents corrupting shared slots)
- `static InstrumentStore d` in callbacks avoids stack overflow (6.4KB struct)

## Subscribing to New Paths

The dynamic `InstrumentStore` replaces the old fixed `InstrumentData` struct.
Apps no longer hardcode instrument fields. To display any SignalK path:

1. `signalk_client_subscribe("path.name", period_ms)` — adds slot to store + sends WS subscribe
2. `signalk_client_watch("path.name", callback, user_data)` — observer fires on LVGL thread
3. In callback: `store_find(store, "path.name")` → read `.value`, `.valid`, `.last_update_ms`
4. On cleanup: `signalk_client_watch(NULL, NULL, NULL)` — clears the watch

For polling apps (autopilot, wind rose): `signalk_client_get_data(&store)` copies the full
store snapshot under mutex. Use `store_find()` on the copy.

## Key Files for Each Concern

| Concern | File |
|---------|------|
| Board init / display / touch | `main/main.cpp` |
| WiFi scan / connect / NVS creds | `components/wifi_manager/wifi_manager.hpp` → public API |
| WiFi Settings app (on-device UI) | `components/wifi_settings_app/wifi_settings_app.cpp` → two-view UI |
| mDNS discovery + reconnect | `main/main.cpp` → `discover_signalk_uri()`, `status_timer_cb()` |
| Connection state machine | `components/signalk_client/signalk_client.hpp` → `signalk_conn_state_t` |
| Launcher status overlay | `main/main.cpp` → `update_status_overlay()`, `sk_logo_alpha.h` |
| SignalK paths subscribed | `components/signalk_client/signalk_client.cpp` → `s_initial_subs[]` + dynamic `subscribe()` |
| Dynamic data store | `components/signalk_client/signalk_client.hpp` → `InstrumentStore`, `PathValue`, `store_find/set` |
| Observer (value watch) | `components/signalk_client/signalk_client.cpp` → `signalk_client_watch()`, `lv_async_call` |
| Data browser (path list + detail) | `components/data_browser_app/data_browser_app.cpp` → HTTP fetch + observer |
| Server base URL | `components/signalk_auth/signalk_auth.hpp` → `signalk_auth_get_base_url()` |
| Device auth / token / API calls | `components/signalk_auth/signalk_auth.hpp` → public API |
| Autopilot button → API wiring | `components/autopilot_app/autopilot_app.cpp` → `on_btn_event()` |
| Wind rose corner panels | `components/wind_rose_app/wind_rose_app.cpp` → `create_corner_cell()` |
| Unit conversions + preferences | `components/unit_config/unit_config.hpp` → standalone API |
| Settings app (timezone + units) | `components/settings_app/settings_app.cpp` → generic picker |
| Status bar (WiFi icon + clock) | `main/main.cpp` → `update_status_bar_wifi()`, `update_status_bar_clock()` |
| Audio feedback (click/sweep/buzz) | `components/audio_feedback/audio_feedback.hpp` |
| Custom font generation | `docs/FONTS.md`, `fonts/Montserrat-Bold.ttf` |
| Flash/partition layout | `partitions.csv` |
| IDF Kconfig / build flags | `sdkconfig.defaults` |

## UI Visual Verification Workflow

**Always crop before judging.** The simulator renders 720×720 images. At thumbnail
size, small changes (dot position, font size, spacing) are invisible. Use Python PIL
to crop the region of interest and read the cropped image:

```python
from PIL import Image
img = Image.open('simulator/screenshots/windrose.png')
crop = img.crop((x1, y1, x2, y2))  # pixel coords of area of interest
crop.save('simulator/screenshots/crop_name.png')
```

Then read the cropped PNG to verify. This prevents hallucinating correctness from
a tiny thumbnail. Common crop regions:
- **Wind rose centre disc**: `(240, 240, 480, 430)`
- **WiFi known networks area**: `(0, 40, 720, 180)`
- **Keyboard area**: `(0, 350, 720, 720)`

**Workflow:**
1. Make the change
2. `cmake --build simulator/build && simulator/render.sh <page>`
3. Crop the affected area with PIL
4. Read the crop — verify the change is actually visible
5. Only then declare it done

## Known Bugs & Edge Cases

### Fixed in this session

| Bug | Root Cause | Fix |
|-----|-----------|-----|
| Token validation always "succeeded" | `/signalk/v1/auth/validate` returns 404 (endpoint doesn't exist in SignalK v2.19.0), but code only rejected 401/403 | Changed to `GET /signalk/v1/api/vessels/self` (real protected endpoint), only 200 = valid |
| Token cleared on transient network errors | `validate_token()` cleared token on any non-401/403 failure | Now only clears on 401/403; network errors return false but preserve token |
| Boot used hardcoded Kconfig credentials | `wifi_init()` in main.cpp used `CONFIG_WIFI_SSID`/`CONFIG_WIFI_PASSWORD` | Replaced with `wifi_manager_connect_saved()` — NVS-first, no Kconfig fallback |
| `connect_saved()` picked first NVS slot blindly | No cross-reference with scan results | Now does blocking boot scan, picks strongest saved network actually in range |
| Disconnect during connect misread as failure | `esp_wifi_disconnect()` in `connect()` fired disconnect event while state was CONNECTING | Added `s_intentional_disconnect` flag to suppress the spurious event |
| Wind rose decimal dot overlapped digits | `dot_y = 30` too high for 76px font | Changed to `dot_y = 50` |
| SignalK logo missing stripe on launcher | `sk_logo_alpha.h` was solid fill, no stripe cutout | Regenerated alpha map from reference logo with stripe as transparent gap |
| Stale fallback URI caused 30s+ connection delay | `CONFIG_SIGNALK_WS_URI` had old IP, mDNS timeout fell back to it | Removed all fallback URI paths — pure mDNS only, retry on failure |
| Unit preferences lost on reboot | `unit_config_init()` called before `nvs_flash_init()` | Moved after `wifi_manager_init()` which initializes NVS |

### Known limitations / areas for investigation

| Area | Description | Impact |
|------|-------------|--------|
| Autopilot auth state display | The update loop only transitions auth state forward (NONE→AUTHORIZED), not backward. If token is cleared while app is open, UI may show stale "Authorized" until menu is reopened | Low — menu re-read on open catches it |
| WiFi app credential save timing | On connect success, saves whatever is in text fields. If user connected via "known networks" quick-connect and text fields are empty, could save empty strings | Low — quick-connect fills text fields before connecting |
| `idf.py erase-flash` destroys all NVS | Erases WiFi credentials AND SignalK auth token. User must re-pair after erase | By design, but can surprise — only use for clean slate |
| `sdkconfig` overrides `sdkconfig.defaults` | Adding a font to `sdkconfig.defaults` has no effect if `sdkconfig` already exists with it disabled. Must also update `sdkconfig` or delete it | Document: `sdkconfig.defaults` changes need `rm sdkconfig && idf.py build` |
| Blocking boot scan delays boot by ~2-3s | `wifi_manager_init()` runs a blocking `esp_wifi_scan_start()` | Acceptable tradeoff — needed for `connect_saved()` to work correctly |
| SignalK server `readOnlyAccess: true` | Dev server with security enabled allows read without auth. Token validation via `/api/vessels/self` returns 200 even with dummy tokens | Only affects dev testing — production servers enforce auth properly |
