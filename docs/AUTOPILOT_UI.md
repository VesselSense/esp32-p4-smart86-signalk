# Autopilot App

Autopilot controller display on the ESP32-P4 720×720 touchscreen.

## Architecture

- Custom-drawn scrolling compass using LVGL draw primitives (`lv_draw_arc`, `lv_draw_line`, `lv_draw_label`) in a `LV_EVENT_DRAW_MAIN_END` callback — NOT using `lv_meter`
- The compass scrolls with the heading — heading is always centred at 12 o'clock
- Angular mapping: 130° of compass displayed in 180° of visual arc (1.38:1 stretch)
- Arc redraws on each heading update via `lv_obj_invalidate()`
- Wind bar has its own draw callback for the SignalK connection logo
- Button state machine manages autopilot mode and engagement locally

## Current Layout (720×720)

```
┌──────────────────────────────────────┐  y=0
│  Rudder Bar (44px, white bg)          │
│  ±30° vertical ticks, no horiz line   │
│  Numbers: port→right, stbd→left       │
│  Green/red fill from center (rudder)  │
│  SignalK logo (56×40) top-right       │
├──────────────────────────────────────┤  y=44
│  Pilot mode (left, m48, white)        │  dark bg
│  Mode status (right, m48, white)      │  above arch
│                                        │
│  Scrolling 180° Semicircular Compass   │
│  • 3-layer band: white base (full),   │
│    red/green overlay (outer 1/3)       │
│  • Orange heading indicator at 12 o'clk│
│  • Black ticks + dark labels on band  │
│  • Lubber triangle (gray, blue area)  │
│  • Yellow wind triangle (wind mode)   │
│  • Green set-angle line+triangle      │
│    (auto mode, pulsing)               │
│                                        │
│         215°  (144px Roboto Bold)      │
│       LH          Mag  (14px font)    │
│           Track         (14px font)   │
│                                        │
├──────────────────────────────────────┤
│  Mode   -1°    +1°    Menu  (3/4h)   │
│  STBY   -10°   +10°   Auto  (3/4h)   │
└──────────────────────────────────────┘  y=720
```

## Display Elements

The autopilot display includes these elements:

1. **Rudder position** — bar at the TOP showing rudder deflection
2. **Pilot Mode** (left label) — "Wind Vane A", "Compass", "Track"
3. **Mode status** (right label) — locked value or live data
4. **Partial compass** — scrolling arc band
5. **Wind direction indicator** — yellow marker on arc at wind angle
6. **Heading** — large heading number in arc interior
7. **Mag/True** — heading source indicator
8. **Track** — mode label below heading

## Button State Machine

### Buttons

| Row 1 | Mode | -1° | +1° | Menu |
|-------|------|-----|-----|------|
| Row 2 | STBY | -10° | +10° | Auto |

**Mode** — cycles autopilot steering mode: Compass → Wind → Track.

**Auto** — engages autopilot in the currently selected mode. Locks heading (compass) or wind angle (wind) at the moment of engage.

**STBY** — disengages autopilot, returns to standby.

**±1° / ±10°** — adjusts locked value when engaged (heading in compass mode, wind angle in wind mode). No effect in standby or track mode.

**Menu** — reserved for SignalK access request flow (see below).

### Label Behavior

| State | Left Label | Right Label |
|-------|-----------|-------------|
| No SignalK connection | (current mode) | "--" |
| Standby + Compass | "Compass" | Live heading ("215°") |
| Standby + Wind | "Wind" | Live wind angle ("38°P") |
| Standby + Track | "Track" | Live XTE or "--" |
| Auto + Compass | "Compass" | Locked heading ("215°") — static |
| Auto + Wind | "Wind" | Locked wind angle ("38°P") — static |
| Auto + Track | "Track" | Live XTE or "--" |

### Wind Mode Behavior

When the autopilot is engaged in wind mode, the locked wind angle is the **setpoint** — the target the autopilot maintains. The heading drifts as the autopilot chases wind shifts. The wind angle display stays steady at the setpoint while the heading (large center number) is the output that changes.

A persistent wind shift causes the heading to drift. The wind shift alarm fires if drift exceeds the configured threshold.

### Visual Indicators

- **Active button pulse**: STBY or Auto text opacity pulses (100%↔70%, 3.2s cycle) to indicate current engagement state
- **Yellow wind triangle**: shows live wind direction on arc — only visible in Wind mode
- **Green set-angle marker**: radial line + triangle on arc at locked setpoint — only visible when engaged in Compass or Wind mode, pulses in sync with Auto button

## Rudder Position Bar (44px)

The top bar is a **rudder position indicator**.

- White background, no border
- 7 uniform vertical ticks (including center), height = wind_h - 8
- Scale: ±30° rudder deflection
- Tick numbers: port side right of tick, starboard side left of tick, vertically centered
- Green/red fill bar from center (behind ticks in z-order), same height as ticks
  - Green = starboard rudder, Red = port rudder
  - Driven by `steering.rudderAngle` from SignalK (PGN 127245)
  - When rudder data unavailable, bar stays centered (zero deflection)
- SignalK logo (56×40, 1-bit bitmap) drawn via `wind_bar_draw_cb` — top-right corner
- Logo color: green when connected, red when disconnected

## Arc Band Structure

Three-layer approach:

1. **White base** — full `arc_w` width, full 180° semicircle
2. **Red/green overlay** — outer 1/3 of band width only
   - Red (port): 180°–270° (left 50%)
   - Green (stbd): 270°–360° (right 50%)
3. **Outer border** — 2px dark outline at `r_outer` (no inner border)

Labels (S, 240, W, N, etc.) sit in the white inner 2/3 of the band.

## Custom Font

- **File**: `components/autopilot_app/lv_font_roboto_96.c`
- **Source**: Roboto-Bold.ttf, 144px, 4bpp, no compression
- **Glyphs**: digits 0-9, degree symbol (°), space, hyphen (-)
- **Symbol**: `lv_font_roboto_96` (name is legacy — actually 144px bold)
- **Requirements**: `LV_FONT_FMT_TXT_LARGE=1` in lv_conf.h / sdkconfig.defaults
- **Must use `--no-compress`** — RLE compressed fonts fail in our LVGL 8.4 setup

## Data Sources

| Data | SignalK Path | PGN | Usage |
|------|-------------|-----|-------|
| Heading (mag) | `navigation.headingMagnetic` | 127250 | Compass display, locked heading |
| Heading (true) | `navigation.headingTrue` | 127250 | Fallback heading source |
| Wind angle (AWA) | `environment.wind.angleApparent` | 130306 | Wind indicator, locked wind angle |
| Rudder angle | `steering.rudderAngle` | 127245 | Rudder position bar |
| XTE | `navigation.courseRhumbline.crossTrackError` | 129283 | Track mode display |
| Connection | `signalk_client_is_connected()` | — | Logo color |

## SignalK Connection Logo

- **Source**: `components/autopilot_app/signalk_logo.h` — 56×40 1-bit bitmap
- **Rendering**: pixel-by-pixel in `wind_bar_draw_cb` using `lv_draw_rect`
- **Position**: top-right of rudder bar
- **Colour**: green (`#00cc66`) when connected, red (`#cc3333`) when disconnected
- **Invalidation**: bar redrawn on connection state change in `update_display()`

## Key Files

| File | Purpose |
|------|---------|
| `components/autopilot_app/autopilot_app.hpp` | Class declaration, enums, state machine |
| `components/autopilot_app/autopilot_app.cpp` | UI build + compass draw + wind bar draw + button handlers + display update |
| `components/autopilot_app/lv_font_roboto_96.c` | Custom 144px Roboto Bold font |
| `components/autopilot_app/signalk_logo.h` | 56×40 1-bit SignalK logo bitmap |
| `components/autopilot_app/CMakeLists.txt` | Component build |
| `simulator/main.cpp` | SimulatorAutopilotApp subclass |

---

## Autopilot Control — Implementation Roadmap

Currently buttons manage **local state only**. The following documents the research and plan for connecting to SignalK autopilot control.

### SignalK Autopilot API Architecture

#### v1 vs v2 API

SignalK has **two** autopilot API layers:

**v1 PUT API** (`/signalk/v1/api/vessels/self/steering/autopilot/...`):
- Direct path-based PUTs to SignalK data model
- Used by the `@signalk/signalk-autopilot` plugin's own backend
- No provider abstraction — the plugin handles hardware directly
- Example: `PUT /signalk/v1/api/vessels/self/steering/autopilot/state {"value": "auto"}`

**v2 Autopilot API** (`/signalk/v2/api/vessels/self/autopilots/{id}/...`):
- Device-agnostic abstraction layer built into SignalK server
- **Requires a registered provider plugin** — the server delegates all calls to the provider
- Provider implements the `AutopilotProvider` interface (engage, disengage, setState, setMode, etc.)
- If no provider is registered, `/autopilots` returns `{}` and all calls return 500

**Our device uses the v2 API** because it's the standard going forward and works with any
provider plugin.

#### Provider Plugin: `@signalk/signalk-autopilot`

The `@signalk/signalk-autopilot` plugin serves dual roles:
1. **Web GUI** — autopilot remote control emulator in the browser
2. **v2 API Provider** — registers as an `AutopilotProvider` with the server

It supports:
- **Raymarine N2K** (`"type": "raymarineN2K"`) — sends PGN 65379 SeatalkPilotMode via NMEA2000
- **Raymarine SeaTalk 1** (`"type": "raymarineST"`) — via SeaTalk-to-NMEA0183 converter
- **Simrad** (`"type": "simrad"`) — NAC-3 via N2K
- **Emulator** (`"type": "emulator"`) — in-memory mock for development

Config: `~/.signalk/plugin-config-data/autopilot.json`

For Raymarine EV-100 on NMEA2000 → `"type": "raymarineN2K"`.

#### Raymarine State Mapping

Raymarine autopilots don't have separate "mode" and "state" concepts. The **state IS the mode**:

| State Value | Meaning | Engaged |
|------------|---------|---------|
| `standby` | Not steering | No |
| `auto` | Compass mode — hold heading | Yes |
| `wind` | Wind mode — hold wind angle | Yes |
| `route` | GPS/Track mode — follow route | Yes |

The v2 `/mode` endpoint is **not implemented** by the signalk-autopilot plugin (`setMode` throws
"Not implemented!"). Instead, use `/state` to both engage and set mode simultaneously.

#### Actual Button → API Mapping (as implemented)

All calls use `PUT /signalk/v2/api/vessels/self/autopilots/_default/state` with auth token.

| Button | When | API Call |
|--------|------|---------|
| Auto (compass) | Standby → engaged | `PUT .../state {"value":"auto"}` |
| Auto (wind) | Standby → engaged | `PUT .../state {"value":"wind"}` |
| Auto (track) | Standby → engaged | `PUT .../state {"value":"route"}` |
| STBY | Always | `PUT .../state {"value":"standby"}` |
| Mode | While engaged | `PUT .../state` with new mode's state value |
| Mode | While standby | **Local only** — no API call (sets UI mode for next engage) |
| ±1°/±10° | While engaged | `PUT .../target/adjust {"value":N,"units":"deg"}` |

#### v2 API Endpoints (full reference)

Base path: `/signalk/v2/api/vessels/self/autopilots/{id}/`

| Endpoint | Method | Purpose | Request Body |
|----------|--------|---------|--------------|
| `/state` | PUT | Set state (and implicitly mode) | `{"value": "auto"/"wind"/"route"/"standby"}` |
| `/engage` | POST | Engage (uses last state or default) | — |
| `/disengage` | POST | Disengage (standby) | — |
| `/mode` | PUT | Set mode (NOT implemented by Raymarine) | `{"value": "compass"/"wind"/"gps"}` |
| `/target` | PUT | Set absolute target | `{"value": 215, "units": "deg"}` |
| `/target/adjust` | PUT | Adjust target ±N° | `{"value": -10, "units": "deg"}` |
| `/tack/port` | POST | Tack to port | — |
| `/tack/starboard` | POST | Tack to starboard | — |
| `/gybe/port` | POST | Gybe to port | — |
| `/gybe/starboard` | POST | Gybe to starboard | — |
| `/dodge` | POST/PUT/DELETE | Enter/adjust/cancel dodge | varies |

#### Autopilot State Paths (subscribe for feedback)

| SignalK Path | Purpose |
|-------------|---------|
| `steering.autopilot.state` | "standby", "auto", "wind", "route" |
| `steering.autopilot.engaged` | true/false |
| `steering.autopilot.target.headingMagnetic` | Locked heading (radians) |
| `steering.autopilot.target.windAngleApparent` | Locked wind angle (radians) |

### SignalK Authentication & Access Requests

Autopilot PUT commands require **admin or write permissions**. The device must authenticate with the SignalK server.

#### Device Access Request Flow

1. **Device generates a UUID** (`clientId`) — stored persistently in NVS
2. **POST** to `/signalk/v1/access/requests`:
   ```json
   {"clientId": "uuid-here", "description": "Autopilot Controller"}
   ```
3. Server responds `202` with `{"state": "PENDING", "href": "/signalk/v1/access/requests/{requestId}"}`
4. **Device polls** `GET /signalk/v1/access/requests/{requestId}` periodically
5. **Admin approves** via SignalK web UI → server returns:
   ```json
   {"state": "COMPLETED", "statusCode": 200,
    "accessRequest": {"permission": "APPROVED", "token": "eyJ..."}}
   ```
6. **Device stores token** in NVS and includes it in all subsequent requests

#### Menu Button Role

The **Menu** button will trigger the access request flow:
- If no token stored: initiate access request, show "Pairing..." on display
- If token exists but expired/rejected: re-request
- If token valid: show settings/configuration

### Raymarine Provider

The `signalk-raymarine-autopilot` plugin translates the v2 API to Raymarine-specific N2K PGNs (proprietary Raymarine commands via SeaTalkNG). Required on the SignalK server.

Available modes: `compass`, `wind`, `gps` (track)

### Implementation Steps

1. **HTTP client**: add `esp_http_client` for REST API calls to SignalK server
2. **Token management**: store/retrieve access token from NVS
3. **Menu button handler**: trigger access request flow
4. **Button → API calls**: replace local state changes with HTTP PUT/POST to autopilot API
5. **State feedback**: subscribe to `steering.autopilot.*` paths to update display from actual autopilot state
6. **Error handling**: display errors (e.g., "No Pilot", permission denied) on screen
