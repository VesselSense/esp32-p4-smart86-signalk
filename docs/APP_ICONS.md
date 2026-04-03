# App Launcher Icons

Custom launcher icons for esp-ui phone apps. Each icon is a 112×112 `lv_img_dsc_t`
in `LV_IMG_CF_TRUE_COLOR_ALPHA` format, displayed by esp-ui at ~70% zoom (~90px)
inside a 128×128 transparent container on the dark launcher home screen.

---

## Icon Format

| Property | Value |
|----------|-------|
| Size | 112×112 px |
| Color format | `LV_IMG_CF_TRUE_COLOR_ALPHA` |
| Color depth | 16-bit RGB565 + 1 byte alpha = **3 bytes/pixel** |
| Total data | 112 × 112 × 3 = 37,632 bytes |
| Background | White rounded rect (`#F5F4F3`), corner radius ~32px, anti-aliased edges |
| Alpha | 0x00 = transparent (outside rounded rect), 0xFF = opaque (inside) |
| Byte order | RGB565 little-endian (no swap), then alpha byte |

**Critical**: The firmware uses `CONFIG_LV_COLOR_DEPTH=16` (not 32). Icon pixel data
must be RGB565. Check `build/config/sdkconfig.h` to confirm. The simulator uses 32-bit
(`simulator/lv_conf.h`), so the launcher grid preview is approximate — the device is
the ground truth.

---

## Default Icon Reference

The built-in esp-ui icon at:
```
managed_components/espressif__esp-ui/src/systems/phone/assets/esp_ui_phone_app_launcher_image_default.c
```

Key properties (measured from the 16-bit pixel data):
- White rounded-corner square with subtle gradient and shadow
- Corner radius: ~32px (first fully opaque pixel at x=32 on row 0)
- Interior color: `#F5F4F3` (off-white)
- Contains `#if` sections for 8-bit, 16-bit, 16-bit-swapped, and 32-bit color depths
- Data size: `12544 * LV_IMG_PX_SIZE_ALPHA_BYTE` (evaluated at compile time)

---

## Workflow: Screenshot to Launcher Icon

### Step 1 — Source image

Start with a reference image (screenshot, PNG, etc.) of the desired icon shape.
Single-color vector shapes work best at 112px. The shape should be high contrast
on a plain background for clean extraction.

### Step 2 — Extract alpha map

Use Python + Pillow to convert the image to a 112×112 alpha map header file.
This captures the shape as `0x00` (transparent) / `0xFF` (opaque) per pixel,
with anti-aliased edges via LANCZOS downscale.

```python
from PIL import Image
import numpy as np

img = Image.open("source_image.png")
gray = np.array(img.convert("L"))

# Find bounding box of dark content
mask = gray < 180
rows = np.any(mask, axis=1)
cols = np.any(mask, axis=0)
rmin, rmax = np.where(rows)[0][[0, -1]]
cmin, cmax = np.where(cols)[0][[0, -1]]

# Crop to square with padding, resize to 112×112
# ... (centre and pad to square) ...
cropped_img = Image.fromarray(cropped, mode="L")
small = np.array(cropped_img.resize((112, 112), Image.LANCZOS))

# Invert (dark shape → high alpha) and boost contrast
alpha = np.clip((255 - small.astype(float)) * 2.5, 0, 255).astype(np.uint8)
alpha[alpha < 15] = 0  # kill background noise
```

Save as a C header (`component_dir/icon_alpha.h`):
```c
#pragma once
#include <stdint.h>
#define ICON_WIDTH  112
#define ICON_HEIGHT 112
static const uint8_t icon_alpha[112][112] = { ... };
```

### Step 3 — Generate `lv_img_dsc_t` C file

Composites the icon shape onto a white rounded-rect background in **16-bit RGB565
+ alpha** format. The rounded rect must be supersampled (4×) for smooth edges.

```python
from PIL import Image, ImageDraw

W, H = 112, 112
SCALE = 4
corner_radius = 32  # match default icon

# Anti-aliased rounded rect mask (draw at 4× then downscale)
mask_big = Image.new("L", (W*SCALE, H*SCALE), 0)
draw = ImageDraw.Draw(mask_big)
draw.rounded_rectangle([0, 0, W*SCALE-1, H*SCALE-1],
                        radius=corner_radius*SCALE, fill=255)
bg_mask = np.array(mask_big.resize((W, H), Image.LANCZOS))

# Shrink icon shape for padding inside rounded rect
helm_resized = helm_img.resize((88, 88), Image.LANCZOS)
# Centre in 112×112 ...

# RGB565 conversion
def rgb888_to_rgb565(r, g, b):
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    val = (r5 << 11) | (g6 << 5) | b5
    return val & 0xFF, (val >> 8) & 0xFF  # little-endian

# Write 3 bytes per pixel: RGB565_lo, RGB565_hi, alpha
for each pixel:
    if outside_rounded_rect:  [0x00, 0x00, 0x00]
    elif icon_shape:          [lo, hi, bg_alpha]  # blended color
    else:                     [bg_lo, bg_hi, bg_alpha]  # white bg
```

Output C file structure:
```c
#include "lvgl.h"
static const uint8_t icon_map[] = {
#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 0
  /* RGB565 + alpha pixel data */
  0xlo, 0xhi, 0xalpha, ...
#else
  #error "Icon only supports LV_COLOR_DEPTH 16, no swap"
#endif
};
const lv_img_dsc_t my_icon = {
  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
  .header.w = 112, .header.h = 112,
  .data_size = 12544 * LV_IMG_PX_SIZE_ALPHA_BYTE,
  .data = icon_map,
};
```

### Step 4 — Wire into the app

In the app's `CMakeLists.txt`, add the `.c` file to SRCS:
```cmake
idf_component_register(
    SRCS "my_app.cpp" "my_icon.c"
    ...
)
```

In the app's `.cpp` constructor:
```cpp
LV_IMG_DECLARE(my_icon);

MyApp::MyApp()
    : ESP_UI_PhoneApp("My App",
                      &my_icon,        // ← custom icon
                      true, false, false)
{}
```

### Step 5 — Validate in simulator

Add the icon `.c` file to `simulator/CMakeLists.txt` and render the launcher grid:

```sh
simulator/render.sh launcher-grid
# → simulator/screenshots/launcher_grid.png
```

The `launcher-grid` mode in `simulator/main.cpp` renders all 4 app icons at the
actual displayed size (~90px in 128px containers) on the dark launcher background.
Review with the Read tool before flashing.

**Note**: The simulator uses `LV_COLOR_DEPTH 32` while the device uses 16-bit.
The launcher grid preview is visually accurate for layout and shape but uses 32-bit
pixel data from the same `lv_img_dsc_t`. For exact pixel-level fidelity, the device
is the ground truth.

### Step 6 — Build and flash

```sh
idf.py build
idf.py -p /dev/cu.usbmodem* flash
```

If the icon doesn't change after flashing, verify:
1. **Compile time** in serial output matches the current build (stale cache)
2. **`helm_icon.c.obj` was recompiled** — check build output for `Building C object ...helm_icon.c.obj`
3. If needed, delete the cached `.obj` file and rebuild, or run `idf.py fullclean`

---

## Simulator Launcher Grid

The `launcher-grid` render mode (`simulator/render.sh launcher-grid`) displays the
actual `lv_img_dsc_t` icons using `lv_img` widgets, mimicking the esp-ui home screen:

- 4 icons in a horizontal row, centred on 720×720
- Each icon zoomed to 70% (`lv_img_set_zoom(img, 179)`) matching esp-ui's display
- App name labels below each icon
- Dark launcher background (`#1A1A2E`)

The real esp-ui default icon asset and any custom icons are compiled into the
simulator binary via `simulator/CMakeLists.txt`.

---

## esp-ui Launcher Internals

Source: `managed_components/espressif__esp-ui/src/widgets/app_launcher/`

| Property | Value |
|----------|-------|
| Icon container | 128×128px, transparent bg, no border, radius=0 |
| Image zoom | 70% of container (~90px from 112px source) |
| Pressed zoom | 63% of container (~81px) |
| Grid layout | Row-wrap flex, dynamically calculated column/row padding |
| Widget | `lv_img` (standard LVGL image, not imgbtn) |
| Name label | Below icon container |

The launcher does NOT apply rounded corners — they must be baked into the icon image.

---

## Files

| File | Purpose |
|------|---------|
| `components/*/my_icon_alpha.h` | Alpha map header (intermediate, from Step 2) |
| `components/*/my_icon.c` | Final `lv_img_dsc_t` with RGB565+alpha pixel data |
| `simulator/main.cpp` | `render_launcher_grid()` — launcher preview renderer |
| `simulator/CMakeLists.txt` | Must include icon `.c` files for simulator build |
| `managed_components/.../esp_ui_phone_app_launcher_image_default.c` | Default icon reference |

---

## Related Docs

- [SIMULATOR.md](SIMULATOR.md) — Simulator architecture, render modes, stubs
- [esp-brookesia.md](esp-brookesia.md) — esp-ui API, `ESP_UI_PhoneApp` constructor, icon declaration
- [BUILD.md](BUILD.md) — Build issues, component dependencies, sdkconfig
- [FONTS.md](FONTS.md) — Custom LVGL font generation (same `lv_font_conv` toolchain)
- [SETUP.md](SETUP.md) — Build/flash workflow, adding instruments
