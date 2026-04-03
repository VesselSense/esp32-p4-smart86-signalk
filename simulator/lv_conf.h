#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Color depth: 32 = ARGB8888 */
#define LV_COLOR_DEPTH 32

/* Memory */
#define LV_MEM_SIZE (2 * 1024 * 1024U)

/* Fonts */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_FMT_TXT_LARGE 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Layouts */
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/* Widgets */
#define LV_USE_ARC   1
#define LV_USE_BAR   1
#define LV_USE_LABEL 1
#define LV_USE_LINE  1
#define LV_USE_IMG    1
#define LV_USE_CANVAS 1
#define LV_USE_TABLE 0
#define LV_USE_CHART 0

/* Logging */
#define LV_USE_LOG 0

/* Drawing */
#define LV_DRAW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_CIRCLE_CACHE_SIZE 4

/* Disable unused features to keep build fast */
#define LV_USE_THEME_DEFAULT    1
#define LV_USE_THEME_BASIC      0
#define LV_USE_THEME_MONO       0
#define LV_THEME_DEFAULT_DARK   1

#define LV_USE_ANIMIMG     0
#define LV_USE_CALENDAR    0
#define LV_USE_COLORWHEEL  0
#define LV_USE_IMGBTN      0
#define LV_USE_KEYBOARD    1
#define LV_USE_LED         0
#define LV_USE_LIST        1
#define LV_USE_MENU        0
#define LV_USE_METER       1
#define LV_USE_MSGBOX      0
#define LV_USE_ROLLER      0
#define LV_USE_SLIDER      0
#define LV_USE_SPAN        0
#define LV_USE_SPINBOX     0
#define LV_USE_SPINNER     0
#define LV_USE_TABVIEW     0
#define LV_USE_TILEVIEW    0
#define LV_USE_WIN         0
#define LV_USE_TEXTAREA    1
#define LV_USE_DROPDOWN    0
#define LV_USE_CHECKBOX    0
#define LV_USE_SWITCH      0
#define LV_USE_BTN         1
#define LV_USE_BTNMATRIX   1

#define LV_USE_SNAPSHOT    0
#define LV_USE_MONKEY      0
#define LV_USE_GRIDNAV     0
#define LV_USE_FRAGMENT    0
#define LV_USE_IMGFONT     0
#define LV_USE_MSG         0
#define LV_USE_IME_PINYIN  0

#endif /* LV_CONF_H */
