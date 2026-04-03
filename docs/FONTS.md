# Custom Font Generation

## Prerequisites

- **`lv_font_conv`** — LVGL font converter (Node.js)
  ```sh
  npm install -g lv_font_conv
  ```
- **Source fonts** stored in `fonts/` (checked into repo)
  - `Montserrat-Bold.ttf` — from [Google Fonts / Montserrat](https://github.com/JulietaUla/Montserrat)

## Generating a Custom LVGL Font

```sh
lv_font_conv \
  --font fonts/Montserrat-Bold.ttf \
  --size <PX> \
  --bpp 4 \
  --range 0x20,0x2D-0x2E,0x30-0x39,0xB0 \
  --format lvgl \
  --no-compress \
  --lv-include lvgl.h \
  --output components/<app>/lv_font_montserrat_<PX>.c
```

### Key flags

| Flag | Purpose |
|------|---------|
| `--size <PX>` | Pixel height of the rendered glyphs |
| `--bpp 4` | 4-bit antialiasing (16 levels) |
| `--range` | Unicode codepoints to include (see below) |
| `--no-compress` | Required — RLE compression fails in our LVGL 8.4 setup |
| `--lv-include lvgl.h` | Header include path for generated file |

### Common glyph ranges

| Range | Characters |
|-------|-----------|
| `0x20` | Space |
| `0x2D-0x2E` | Hyphen `-`, period `.` |
| `0x30-0x39` | Digits `0`–`9` |
| `0xB0` | Degree symbol `°` |

Add more ranges as needed (e.g., `0x41-0x5A` for A–Z uppercase).

## Existing Custom Fonts

| File | Size | Font | Glyphs | Used by |
|------|------|------|--------|---------|
| `components/wind_rose_app/lv_font_montserrat_76.c` | 76px | Montserrat Bold | digits, space, dash | Wind Rose AWS centre |
| `components/wind_rose_app/lv_font_montserrat_67.c` | 67px | Montserrat Bold | digits, space, dash, period, degree | Wind Rose corner values |
| `components/wind_rose_app/lv_font_montserrat_62.c` | 62px | Montserrat Bold | digits, space, dash, period, degree | Wind Rose corner panels |
| `components/wind_rose_app/lv_font_montserrat_bold_50.c` | 50px | Montserrat Bold | digits, space, dash, period, degree | Wind Rose TWS overlay |
| `components/autopilot_app/lv_font_roboto_96.c` | 144px* | Roboto Bold | digits, degree, space, dash | Autopilot heading |
| `components/data_browser_app/lv_font_montserrat_90.c` | 90px | Montserrat Bold | digits, period, dash, degree, percent | Data Browser degree symbol (1/3 of 310) |
| `components/data_browser_app/lv_font_montserrat_120.c` | 210px** | Montserrat Bold | digits, period, dash, degree, percent | Data Browser value (4+ chars) |
| `components/data_browser_app/lv_font_montserrat_310.c` | 310px | Montserrat Bold | digits, period, dash, degree, percent | Data Browser value (3 chars) |
| `components/data_browser_app/lv_font_montserrat_360.c` | 360px | Montserrat Bold | digits, period, dash, degree, percent | Data Browser value (1-2 chars) |

\* The autopilot font is named `_96` but was generated at `--size 144` (legacy naming).
\*\* The data browser `_120` font was regenerated at 210px (name kept for code compatibility).

## After Generating

1. **Add to component CMakeLists.txt** — add the `.c` file to `SRCS`
2. **Add to simulator CMakeLists.txt** — add the source path to the simulator build
3. **Use in code**:
   ```cpp
   LV_FONT_DECLARE(lv_font_montserrat_62);
   lv_obj_set_style_text_font(label, &lv_font_montserrat_62, 0);
   ```
4. **No lv_conf.h changes needed** — custom fonts have their own include guards

## Notes

- `LV_FONT_FMT_TXT_LARGE=1` must be set in `lv_conf.h` / `sdkconfig.defaults` for fonts >50KB
- Standard LVGL built-in sizes (14, 20, 24, 28, 36, 48) are enabled via `sdkconfig.defaults` and `simulator/lv_conf.h` — no generation needed
