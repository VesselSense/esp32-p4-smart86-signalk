#pragma once

#include "esp_ui.hpp"
#include "signalk_client.hpp"
#include <stddef.h>

// Returns true if value is invalid or not updated within CONFIG_DATA_STALE_MS.
bool instrument_is_stale(const PathValue &v);

// Format m/s → knots as "%.1f", or "--" if stale.
void instrument_fmt_knots(char *buf, size_t len, const PathValue &v);

// Build a standard instrument cell (title top-left, large value centre, unit bottom-right).
// Returns the cell; sets *value_label_out to the value label if non-null.
lv_obj_t *instrument_create_cell(lv_obj_t *parent, const char *title,
                                  const char *unit, int32_t w, int32_t h,
                                  lv_obj_t **value_label_out);
