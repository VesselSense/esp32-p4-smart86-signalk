#include "instrument_helpers.hpp"
#include "instrument_theme.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include <stdio.h>

bool instrument_is_stale(const PathValue &v)
{
    if (!v.valid) return true;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return (now - v.last_update_ms) > (uint32_t)CONFIG_DATA_STALE_MS;
}

void instrument_fmt_knots(char *buf, size_t len, const PathValue &v)
{
    if (instrument_is_stale(v)) { snprintf(buf, len, "--"); return; }
    snprintf(buf, len, "%.1f", v.value * 1.94384f);
}

lv_obj_t *instrument_create_cell(lv_obj_t *parent, const char *title,
                                   const char *unit, int32_t w, int32_t h,
                                   lv_obj_t **value_label_out)
{
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_set_size(cell, w, h);
    lv_obj_set_style_bg_color(cell, CLR_CELL_BG, 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(cell, CLR_BORDER, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_radius(cell, 0, 0);
    lv_obj_set_style_pad_all(cell, 6, 0);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_lbl = lv_label_create(cell);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, CLR_TITLE, 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 4, 4);

    lv_obj_t *value_lbl = lv_label_create(cell);
    lv_label_set_text(value_lbl, "--");
    lv_obj_set_style_text_color(value_lbl, CLR_VALUE, 0);
    lv_obj_set_style_text_font(value_lbl, &lv_font_montserrat_48, 0);
    lv_obj_align(value_lbl, LV_ALIGN_CENTER, 0, 6);

    lv_obj_t *unit_lbl = lv_label_create(cell);
    lv_label_set_text(unit_lbl, unit);
    lv_obj_set_style_text_color(unit_lbl, CLR_UNIT, 0);
    lv_obj_set_style_text_font(unit_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(unit_lbl, LV_ALIGN_BOTTOM_RIGHT, -4, -4);

    if (value_label_out) *value_label_out = value_lbl;
    return cell;
}
