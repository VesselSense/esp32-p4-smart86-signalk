# Setup Guide — SignalK Instrument Panel (ESP32-P4-WIFI6-Touch-LCD-4B)

## Hardware

- **Board**: Waveshare ESP32-P4-WIFI6-Touch-LCD-4B ("Smart 86 Box")
- **Display**: 4" IPS 720×720 MIPI-DSI, ST7703 driver
- **Touch**: GT911 (I2C, SDA=GPIO7, SCL=GPIO8)
- **WiFi**: ESP32-C6 coprocessor via SDIO (esp-hosted)
- **Flash**: 16MB · **PSRAM**: 8MB OSPI

---

## Prerequisites

### macOS: Homebrew Python must come first

The IDF toolchain requires Python ≥ 3.10. macOS ships with Xcode Python 3.9
which will be picked up by default if Homebrew is not first in `PATH`. Add
this to `~/.zshrc` **before** installing or using IDF:

```sh
export PATH="/opt/homebrew/bin:$PATH"
alias get_idf='. $HOME/esp/esp-idf/export.sh'
```

Reload your shell (`source ~/.zshrc`) before continuing.

### ESP-IDF v5.4

```sh
git clone --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf
git checkout v5.4
./install.sh esp32p4      # downloads RISC-V toolchain and Python venv
```

The venv will be created at `~/.espressif/python_env/idf5.4_py3.13_env`.
If you see a venv named `idf5.4_py3.9_env` it was built with the wrong
Python — delete it and re-run `install.sh` with Homebrew first in PATH:

```sh
rm -rf ~/.espressif/python_env/idf5.4_py3.9_env
PATH="/opt/homebrew/bin:$PATH" ~/esp/esp-idf/install.sh esp32p4
```

---

## Configuration

### WiFi

WiFi credentials are managed on-device via the **WiFi Settings app** and stored
in NVS (non-volatile storage). No compile-time configuration needed.

**First boot (clean NVS):**
1. Device scans for networks, finds no saved credentials → stays disconnected
2. Launcher shows amber/red status (no WiFi)
3. Open the WiFi Settings app from the launcher
4. Known scanned networks appear at the top if any match saved credentials
5. Type SSID and password, tap Connect
6. On success, credentials are saved to NVS automatically
7. Device discovers SignalK server via mDNS and connects

**Subsequent boots:**
1. Device scans at boot, cross-references scan results with NVS saved networks
2. Connects to the strongest saved network in range — no user action needed
3. If no saved network is in range, stays disconnected until user opens WiFi app

**Switching networks:** Open WiFi Settings app → Disconnect → select or type new network.

**NVS erase:** `idf.py erase-flash` wipes all NVS data including WiFi credentials
**and** SignalK auth tokens. Only use when you need a completely clean slate.

### SignalK Server Discovery

The device finds SignalK servers via mDNS (`_signalk-ws._tcp`). No IP address
configuration needed. The ESP32 and Mac must be on the same **2.4GHz** WiFi
network (the ESP32-C6 does not support 5GHz).

If mDNS doesn't find a server, the device stays disconnected and retries
automatically. No fallback URI — mDNS is the only discovery mechanism.

**Note:** The ESP32-C6 WiFi coprocessor ships pre-flashed by Waveshare — no
additional firmware step required.

---

## Build

Dependencies are declared in `main/idf_component.yml` and downloaded
automatically by the component manager on first build.

```sh
get_idf
idf.py build
```

Expected output ends with:
```
Project build complete.
```

Binary output: `build/waveshare_p4_signalk.bin` (~2MB)

See `docs/BUILD.md` for a detailed account of the component dependencies and
the IDF 5.4-specific build issues that were resolved on this project.

---

## Flash

Connect USB-C to the board's UART port, then:

```sh
get_idf
idf.py -p /dev/cu.usbmodem* flash
```

On Linux the port is typically `/dev/ttyUSB0`.

---

## UI Overview

Five apps appear in the esp-ui phone launcher. Tap an icon to open the app.

### SignalK (4-page panel)

Swipe up/down to scroll between pages:

| Page | Layout | Instruments |
|------|--------|-------------|
| 1 — Navigation | 2×2 numeric | SOG · COG · Heading · STW |
| 2 — Wind | Analog gauge + 2 cells | AWA/TWA compass dial · AWS · TWS |
| 3 — Environment | Numeric cells | Depth · Water Temp · Fresh Water |
| 4 — Electrical | 2×2 numeric | Battery SOC · Voltage · Current · Solar |

See `docs/WIND_GAUGE.md` for a detailed description of the Page 2 wind gauge.

**Status dot** (top-right): green = connected to SignalK, red = disconnected.
Values dim to a grey colour when data has not been updated within
`CONFIG_DATA_STALE_MS` (default 5 000 ms).

### Wind Gauge (standalone)

Full-screen version of the Page 2 wind gauge — same AWA/TWA dial and
AWS/TWS speed strip, fills the whole display.

### Autopilot

Scrolling compass strip, wind bar, and mode/button controls for autopilot
engagement and adjustment. See `docs/AUTOPILOT_UI.md` for design details.

### Wind Rose

Heading-relative compass rose with rotating tick ring and cardinal/degree labels.
The compass rotates so the current magnetic heading is always at 12 o'clock.
Centre disc shows apparent wind speed (AWS) in knots with integer.fraction layout
using a custom 76px Montserrat Bold font. Fixed outer ring has red (port) and
green (starboard) zones.

Data bindings:
- Heading: `navigation.headingMagnetic` (rotates compass rose)
- Wind speed: `environment.wind.speedApparent` (centre display, converted to knots)

Render with simulator: `simulator/render.sh windrose`

### WiFi Settings

On-device WiFi configuration. Two views:
- **Connected view**: current network name + signal strength, Disconnect button,
  list of other saved networks with Switch buttons
- **Disconnected view**: known networks in range (tap to connect with saved password),
  SSID/password text input with on-screen keyboard, Connect button

Credentials are saved to NVS on successful connect and used for automatic
reconnection on boot. See WiFi section above for the full flow.

Render with simulator: `simulator/render.sh wifi`

---

## Adding Instruments

Each instrument requires changes in four files plus a simulator stub.

### Numeric cell (standard pattern)

1. Add `InstrumentValue my_val;` to `InstrumentData` in `signalk_client.hpp`
2. Add path routing in `parse_delta()` in `signalk_client.cpp`, and add the
   path to `send_subscribe()` with an appropriate period (ms)
3. Add `instrument_create_cell()` call in `build_ui()` in `signalk_app.cpp`
4. Add format + `lv_label_set_text()` in `update_display()` in `signalk_app.cpp`
5. Add a representative stub value (SI units) to `simulator/stubs/signalk_client_stub.cpp`
6. Validate: `simulator/render.sh` → inspect `simulator/screenshots/pageN.png`
7. Recompile and flash

### Analog gauge needle (wind gauge pattern)

Steps 1–2 as above, then:

3. Add `lv_meter_indicator_t *_needle;` to `signalk_app.hpp`
4. In `create_wind_page()`: `lv_meter_add_needle_line()` on `_wind.meter`
5. In `update_display()`: `lv_meter_set_indicator_value()` with
   `mv = (int)(deg + 360.5f) % 360`
6. Add stub value, render, flash

See `docs/WIND_GAUGE.md` for the full wind gauge implementation reference.
