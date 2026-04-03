#pragma once

#include "esp_ui.hpp"
#include "signalk_client.hpp"

// Handles returned by wind_gauge_build() — passed back in to wind_gauge_update()
// each timer tick.
struct WindGaugeHandles {
    lv_obj_t             *meter      = nullptr;
    lv_meter_indicator_t *needle_awa = nullptr;  // AWA — cyan
    lv_meter_indicator_t *needle_twa = nullptr;  // TWA — amber
    lv_obj_t             *lbl_awa    = nullptr;  // centre AWA value label
    lv_obj_t             *lbl_aws    = nullptr;  // AWS speed label
    lv_obj_t             *lbl_tws    = nullptr;  // TWS speed label
};

// Build the full wind gauge UI into `parent` (w × h pixels).
// Sets parent to column flex (gauge area above, speed strip below).
// Returns handles needed by wind_gauge_update().
WindGaugeHandles wind_gauge_build(lv_obj_t *parent, int32_t w, int32_t h);

// Update all gauge needles and speed labels from a fresh InstrumentData snapshot.
void wind_gauge_update(const WindGaugeHandles &handles, const InstrumentData &d);
