#pragma once

// Dark marine colour palette — shared by all instrument apps.
// Requires LVGL to be included before this header (lv_color_hex macro).

#define CLR_BG           lv_color_hex(0x0d1b2a)   // deep navy background
#define CLR_CELL_BG      lv_color_hex(0x1b263b)   // cell background
#define CLR_BORDER       lv_color_hex(0x415a77)   // cell border / minor ticks
#define CLR_TITLE        lv_color_hex(0x778da9)   // instrument label / major ticks
#define CLR_VALUE        lv_color_hex(0xe0fbfc)   // live value (cyan)
#define CLR_UNIT         lv_color_hex(0x778da9)   // unit label
#define CLR_STALE        lv_color_hex(0x445566)   // stale / invalid value
#define CLR_CONNECTED    lv_color_hex(0x06d6a0)   // green status dot
#define CLR_DISCONNECTED lv_color_hex(0xef233c)   // red status dot
#define CLR_AWA          lv_color_hex(0xe0fbfc)   // apparent wind needle — cyan
#define CLR_TWA          lv_color_hex(0xffa040)   // true wind needle — amber
#define CLR_PORT         lv_color_hex(0xef233c)   // port arc / label — red
#define CLR_STBD         lv_color_hex(0x06d6a0)   // starboard arc / label — teal
