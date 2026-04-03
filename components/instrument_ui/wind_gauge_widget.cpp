#include "wind_gauge_widget.hpp"
#include "instrument_theme.h"
#include "instrument_helpers.hpp"
#include "unit_config.hpp"
#include <stdio.h>

WindGaugeHandles wind_gauge_build(lv_obj_t *parent, int32_t w, int32_t h)
{
    WindGaugeHandles out;

    // ── Column flex: gauge area on top, speed strip below ─────────────────────
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    int32_t speed_h = h - 580;
    if (speed_h < 100) speed_h = 100;
    int32_t gauge_h = h - speed_h;

    // ── Gauge area (absolute-positioned children) ─────────────────────────────
    lv_obj_t *gauge_area = lv_obj_create(parent);
    lv_obj_set_size(gauge_area, w, gauge_h);
    lv_obj_set_style_bg_color(gauge_area, CLR_BG, 0);
    lv_obj_set_style_bg_opa(gauge_area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(gauge_area, 0, 0);
    lv_obj_set_style_pad_all(gauge_area, 0, 0);
    lv_obj_clear_flag(gauge_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(gauge_area, 0);   // no layout — children use absolute pos

    int32_t gauge_size = ((gauge_h < w) ? gauge_h : w) - 40;
    int32_t r  = gauge_size / 2;
    int32_t cx = w / 2;
    int32_t cy = gauge_h / 2;

    // ── lv_meter: tick ring + close-hauled arcs + needles ─────────────────────
    out.meter = lv_meter_create(gauge_area);
    lv_obj_set_size(out.meter, gauge_size, gauge_size);
    lv_obj_set_pos(out.meter, cx - r, cy - r);
    lv_obj_set_style_bg_opa(out.meter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(out.meter, 0, 0);
    lv_obj_set_style_pad_all(out.meter, 0, 0);

    lv_meter_scale_t *scale = lv_meter_add_scale(out.meter);

    // 73 minor ticks = one per 5°
    lv_meter_set_scale_ticks(out.meter, scale, 73, 1, 10,
                             lv_color_hex(0x415a77));
    // Major ticks every 9th minor (= 45°), longer + brighter, no labels
    lv_meter_set_scale_major_ticks(out.meter, scale, 9, 2, 18,
                                   lv_color_hex(0x778da9), -200);
    // Full circle: value 0..360, rotation 270 → 0 at top (ahead)
    lv_meter_set_scale_range(out.meter, scale, 0, 360, 360, 270);

    // Starboard close-hauled zone: +30°..+60°
    lv_meter_indicator_t *stbd_zone =
        lv_meter_add_arc(out.meter, scale, 22, CLR_STBD, 0);
    lv_meter_set_indicator_start_value(out.meter, stbd_zone, 30);
    lv_meter_set_indicator_end_value(  out.meter, stbd_zone, 60);

    // Port close-hauled zone: -60°..-30° → values 300..330
    lv_meter_indicator_t *port_zone =
        lv_meter_add_arc(out.meter, scale, 22, CLR_PORT, 0);
    lv_meter_set_indicator_start_value(out.meter, port_zone, 300);
    lv_meter_set_indicator_end_value(  out.meter, port_zone, 330);

    // TWA needle (amber, created first → drawn underneath)
    out.needle_twa = lv_meter_add_needle_line(out.meter, scale, 3, CLR_TWA, -30);
    lv_meter_set_indicator_value(out.meter, out.needle_twa, 0);

    // AWA needle (cyan, created second → drawn on top)
    out.needle_awa = lv_meter_add_needle_line(out.meter, scale, 4, CLR_AWA, -20);
    lv_meter_set_indicator_value(out.meter, out.needle_awa, 0);

    // ── Cardinal labels — inside ring at 65% of radius ────────────────────────
    int32_t ri = r * 65 / 100;

    lv_obj_t *lbl_bow = lv_label_create(gauge_area);
    lv_label_set_text(lbl_bow, "BOW");
    lv_obj_set_style_text_font(lbl_bow, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_bow, CLR_TITLE, 0);
    lv_obj_set_pos(lbl_bow, cx - 16, cy - ri - 10);

    lv_obj_t *lbl_port = lv_label_create(gauge_area);
    lv_label_set_text(lbl_port, "P");
    lv_obj_set_style_text_font(lbl_port, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_port, CLR_PORT, 0);
    lv_obj_set_pos(lbl_port, cx - ri - 10, cy - 10);

    lv_obj_t *lbl_stbd = lv_label_create(gauge_area);
    lv_label_set_text(lbl_stbd, "S");
    lv_obj_set_style_text_font(lbl_stbd, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_stbd, CLR_STBD, 0);
    lv_obj_set_pos(lbl_stbd, cx + ri - 5, cy - 10);

    lv_obj_t *lbl_stern = lv_label_create(gauge_area);
    lv_label_set_text(lbl_stern, "STN");
    lv_obj_set_style_text_font(lbl_stern, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_stern, CLR_TITLE, 0);
    lv_obj_set_pos(lbl_stern, cx - 16, cy + ri - 5);

    // ── Centre disc: AWA label + value ────────────────────────────────────────
    int32_t disc_w = 160, disc_h = 84;
    lv_obj_t *disc = lv_obj_create(gauge_area);
    lv_obj_set_size(disc, disc_w, disc_h);
    lv_obj_set_pos(disc, cx - disc_w / 2, cy - disc_h / 2 + 4);
    lv_obj_set_style_bg_color(disc, CLR_BG, 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_90, 0);
    lv_obj_set_style_radius(disc, 12, 0);
    lv_obj_set_style_border_width(disc, 0, 0);
    lv_obj_set_style_pad_all(disc, 0, 0);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl_title = lv_label_create(disc);
    lv_label_set_text(lbl_title, "AWA");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_title, CLR_TITLE, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 4);

    out.lbl_awa = lv_label_create(disc);
    lv_label_set_text(out.lbl_awa, "--");
    lv_obj_set_style_text_font(out.lbl_awa, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(out.lbl_awa, CLR_VALUE, 0);
    lv_obj_align(out.lbl_awa, LV_ALIGN_BOTTOM_MID, 0, -4);

    // ── Needle legend ─────────────────────────────────────────────────────────
    lv_obj_t *legend = lv_obj_create(gauge_area);
    lv_obj_set_size(legend, 120, 20);
    lv_obj_set_pos(legend, cx - 60, cy + disc_h / 2 + 14);
    lv_obj_set_style_bg_opa(legend, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(legend, 0, 0);
    lv_obj_set_style_pad_all(legend, 0, 0);
    lv_obj_clear_flag(legend, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(legend, 8, 0);

    lv_obj_t *leg_awa = lv_label_create(legend);
    lv_label_set_text(leg_awa, "- AWA");
    lv_obj_set_style_text_font(leg_awa, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(leg_awa, CLR_AWA, 0);

    lv_obj_t *leg_twa = lv_label_create(legend);
    lv_label_set_text(leg_twa, "- TWA");
    lv_obj_set_style_text_font(leg_twa, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(leg_twa, CLR_TWA, 0);

    // ── Speed strip: AWS (left) | TWS (right) ─────────────────────────────────
    lv_obj_t *speed_row = lv_obj_create(parent);
    lv_obj_set_size(speed_row, w, speed_h);
    lv_obj_set_style_bg_color(speed_row, CLR_BG, 0);
    lv_obj_set_style_bg_opa(speed_row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(speed_row, 0, 0);
    lv_obj_set_style_pad_all(speed_row, 0, 0);
    lv_obj_set_style_pad_gap(speed_row, 0, 0);
    lv_obj_clear_flag(speed_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(speed_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(speed_row, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    instrument_create_cell(speed_row, "AWS", unit_label(UNIT_CAT_SPEED), w / 2, speed_h, &out.lbl_aws);
    instrument_create_cell(speed_row, "TWS", unit_label(UNIT_CAT_SPEED), w / 2, speed_h, &out.lbl_tws);

    return out;
}

void wind_gauge_update(const WindGaugeHandles &h, const InstrumentData &d)
{
    char buf[16];

    // AWA needle + centre label
    if (h.meter && h.needle_awa) {
        if (!instrument_is_stale(d.wind_angle)) {
            float deg = d.wind_angle.value * 57.2958f;
            int32_t mv = (int32_t)(deg + 360.5f) % 360;
            lv_meter_set_indicator_value(h.meter, h.needle_awa, mv);
            snprintf(buf, sizeof(buf), "%+d\xc2\xb0", (int)deg);
            lv_obj_set_style_text_color(h.lbl_awa, CLR_VALUE, 0);
        } else {
            snprintf(buf, sizeof(buf), "--");
            lv_obj_set_style_text_color(h.lbl_awa, CLR_STALE, 0);
        }
        lv_label_set_text(h.lbl_awa, buf);
    }

    // TWA needle
    if (h.meter && h.needle_twa && !instrument_is_stale(d.wind_angle_true)) {
        float deg = d.wind_angle_true.value * 57.2958f;
        int32_t mv = (int32_t)(deg + 360.5f) % 360;
        lv_meter_set_indicator_value(h.meter, h.needle_twa, mv);
    }

    // AWS speed
    if (instrument_is_stale(d.wind_speed)) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%.1f", unit_convert(UNIT_CAT_SPEED, d.wind_speed.value));
    lv_label_set_text(h.lbl_aws, buf);
    lv_obj_set_style_text_color(h.lbl_aws,
        d.wind_speed.valid ? CLR_VALUE : CLR_STALE, 0);

    // TWS speed
    if (instrument_is_stale(d.wind_speed_true)) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%.1f", unit_convert(UNIT_CAT_SPEED, d.wind_speed_true.value));
    lv_label_set_text(h.lbl_tws, buf);
    lv_obj_set_style_text_color(h.lbl_tws,
        d.wind_speed_true.valid ? CLR_VALUE : CLR_STALE, 0);
}
