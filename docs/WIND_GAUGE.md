# Wind Gauge — Page 2

Analog wind instrument showing Apparent Wind Angle (AWA) and True Wind Angle (TWA)
on a full-circle compass ring, with AWS and TWS speed readouts below.

---

## Visual Layout

```
┌─────────────────────────────────────────────┐
│                                             │  ← page top (y=0)
│        ·  ·  ·  ─  ·  ·  ·                │
│     ·     [RED]    [GRN]     ·             │
│   ·                              ·          │
│  ·   P                      S   ·          │  ← y = cy (horizontal midline)
│   ·                              ·          │
│     ·    ┌──────────┐     ·               │
│       ·  │ AWA      │  ·                  │
│          │  +45°    │                      │
│          └──────────┘                      │
│            - AWA  - TWA                    │
│         ·                ·                 │
│      ·     STN        ·                   │
│        ·  ·  ─  ·  ·                      │
├─────────────────────────────────────────────┤  ← y = gauge_h
│  AWS               │  TWS                  │
│  12.3           KT │  10.0            KT   │
└─────────────────────────────────────────────┘
```

---

## Compass Ring

Implemented with `lv_meter` (LVGL 8.x, requires `LV_USE_METER=1`).

| Parameter | Value |
|-----------|-------|
| Scale range | 0–360 (value units = degrees) |
| Sweep | 360° full circle |
| Rotation | 270° → value 0 appears at 12 o'clock (BOW/ahead) |
| Minor ticks | 73 ticks = one per 5° |
| Major ticks | Every 9th minor = every 45°, longer line, no numeric labels |
| Tick colour | `CLR_BORDER` (#415a77) minor / `CLR_TITLE` (#778da9) major |
| Background | `LV_OPA_TRANSP` — page colour shows through |

### Angle mapping

SignalK delivers wind angles in **radians**, signed:
- Positive → starboard (wind from starboard side)
- Negative → port (wind from port side)

To position a needle on the `lv_meter` scale (0–360, 0 = ahead at top):

```c
float deg = wind_angle_radians * 57.2958f;   // → -180..+180
int32_t mv = (int32_t)(deg + 360.5f) % 360; // → 0..359
lv_meter_set_indicator_value(meter, needle, mv);
```

Examples:
| SignalK (rad) | Degrees | mv | Position |
|---|---|---|---|
| +0.785 | +45° | 45 | Starboard, in green zone |
| −0.611 | −35° | 325 | Port, in red zone |
| +3.14 | +180° | 180 | Dead astern |
| 0 | 0° | 0 | Dead ahead |

---

## Coloured Arc Zones

Built with `lv_meter_add_arc()` on the same scale — no separate `lv_arc` objects needed.

| Zone | Colour | Scale values | Meaning |
|------|--------|-------------|---------|
| Starboard close-hauled | #06d6a0 (teal) | 30–60 | Upwind starboard tack |
| Port close-hauled | #ef233c (red) | 300–330 | Upwind port tack |

Arc width: 22px at `r_mod=0` (outer ring). Adjust `r_mod` to move inward.

To add zones (e.g. a broad-reach zone or dead-run warning):

```c
lv_meter_indicator_t *zone = lv_meter_add_arc(
    _wind_meter, scale,
    22,                        // width px
    lv_color_hex(0xffd166),   // colour
    0);                        // r_mod (0 = outer ring)
lv_meter_set_indicator_start_value(_wind_meter, zone, 120);
lv_meter_set_indicator_end_value(  _wind_meter, zone, 150);
```

---

## Needles

Two needles share the same `lv_meter` scale. Drawing order = indicator creation order
(earlier = drawn first = rendered underneath).

| Needle | Colour | Width | r_mod | Variable | Data source |
|--------|--------|-------|-------|----------|-------------|
| TWA | #ffa040 amber | 3px | −30 | `_wind_needle_twa` | `d.wind_angle_true` |
| AWA | #e0fbfc cyan | 4px | −20 | `_wind_needle` | `d.wind_angle` |

AWA is created second so it renders on top of TWA.

To add a third needle (e.g. True Wind Direction referenced to north):

```c
// In signalk_app.hpp — add member:
lv_meter_indicator_t *_wind_needle_twd = nullptr;

// In create_wind_page() — add after existing needles:
_wind_needle_twd = lv_meter_add_needle_line(
    _wind_meter, scale,
    2,                        // width
    lv_color_hex(0xffd166),  // colour
    -40);                     // r_mod (shorter than others)

// In update_display():
if (!is_stale(d.wind_dir_true)) {
    // TWD is absolute degrees (0–360); map directly
    int32_t mv = (int32_t)(d.wind_dir_true.value * 57.2958f + 0.5f) % 360;
    lv_meter_set_indicator_value(_wind_meter, _wind_needle_twd, mv);
}
```

---

## Cardinal Labels

Positioned inside the ring at ~65% of radius to avoid the tick marks.

```c
int32_t ri = r * 65 / 100;   // inner label radius

// BOW — above centre
lv_obj_set_pos(lbl_bow,  cx - 16, cy - ri - 10);

// PORT — left of centre (red)
lv_obj_set_pos(lbl_port, cx - ri - 10, cy - 10);

// STARBOARD — right of centre (green)
lv_obj_set_pos(lbl_stbd, cx + ri - 5,  cy - 10);

// STERN — below centre
lv_obj_set_pos(lbl_stern, cx - 16, cy + ri - 5);
```

Rule: keep labels at `< 0.75 * r` from centre so they never touch the tick ring.

---

## Centre Disc

A rounded rectangle (`radius=12`) over the gauge centre, background colour `CLR_BG`
at 90% opacity. Prevents the rotating needle from obscuring the AWA readout.

```
disc: 160×84 px, centred at (cx, cy + 4)
  ├── "AWA"   label — CLR_TITLE, Montserrat 14, LV_ALIGN_TOP_MID
  └── "+45°"  label — CLR_VALUE, Montserrat 36, LV_ALIGN_BOTTOM_MID
```

The value label is updated every display tick with the signed integer angle:
`snprintf(buf, sizeof(buf), "%+d\xc2\xb0", (int)deg)` — `+` prefix forces the sign.

---

## Speed Cells (Bottom Strip)

A flex ROW container (full width, `speed_h` px tall) holds two equal `create_cell()`
children:

| Cell | Title | Unit | Label pointer | Source |
|------|-------|------|---------------|--------|
| Left (w/2) | AWS | KT | `_lbl_wind_spd` | `d.wind_speed` |
| Right (w/2) | TWS | KT | `_lbl_tws` | `d.wind_speed_true` |

Both format through `fmt_knots()` which converts m/s → knots (`× 1.94384`).

---

## SignalK Data Sources

| Field | SignalK path | Units | InstrumentData field |
|-------|-------------|-------|---------------------|
| AWA | `environment.wind.angleApparent` | radians (−π..+π) | `wind_angle` |
| AWS | `environment.wind.speedApparent` | m/s | `wind_speed` |
| TWA | `environment.wind.angleTrueWater` | radians (−π..+π) | `wind_angle_true` |
| TWS | `environment.wind.speedTrue` | m/s | `wind_speed_true` |

All four paths are subscribed at 500 ms in `send_subscribe()`.

`angleTrueWater` is the true wind angle relative to the boat's motion through the
water (corrected for leeway). Use `angleTrueGround` instead for COG-referenced true wind.

---

## Simulator Stub Values

`simulator/stubs/signalk_client_stub.cpp` ships with a starboard-tack scenario:

| Value | Radians | Degrees | Knots |
|-------|---------|---------|-------|
| AWA | +0.7854 | +45° | — |
| AWS | — | — | 12.3 kn |
| TWA | +0.9599 | +55° | — |
| TWS | — | — | 10.0 kn |

To test port tack, negate both angle values (−0.7854 / −0.9599).

---

## Extending the Wind Page

### Add a dead-run warning zone
```c
// Running dead downwind: 150°–180° both sides
lv_meter_indicator_t *run_stbd = lv_meter_add_arc(_wind_meter, scale, 14,
    lv_color_hex(0xffd166), -4);   // amber, inside close-hauled arcs
lv_meter_set_indicator_start_value(_wind_meter, run_stbd, 150);
lv_meter_set_indicator_end_value(  _wind_meter, run_stbd, 180);

lv_meter_indicator_t *run_port = lv_meter_add_arc(_wind_meter, scale, 14,
    lv_color_hex(0xffd166), -4);
lv_meter_set_indicator_start_value(_wind_meter, run_port, 180);
lv_meter_set_indicator_end_value(  _wind_meter, run_port, 210);
```

### Change close-hauled zone angles
The 30°–60° / 300°–330° zones are hard-coded constants. To tune them:

```c
// In create_wind_page(), find these calls and change the values:
lv_meter_set_indicator_start_value(_wind_meter, stbd_zone, 30);  // ← change
lv_meter_set_indicator_end_value(  _wind_meter, stbd_zone, 60);  // ← change
```

### Add wind speed as an arc gauge
Replace the AWS numeric cell with an `lv_arc` sweeping 0–40 kt, with the
numeric value overlaid as a label. See `docs/esp-brookesia.md` for the
`lv_arc` API reference.
