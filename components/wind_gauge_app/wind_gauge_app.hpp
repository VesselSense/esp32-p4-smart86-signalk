#pragma once

#include "esp_ui.hpp"
#include "signalk_client.hpp"
#include "wind_gauge_widget.hpp"

// ---------------------------------------------------------------------------
// WindGaugeApp — standalone wind instrument for the esp-ui phone launcher.
//
// Fills the entire visual area with the analog wind gauge:
//   - lv_meter compass ring with AWA (cyan) + TWA (amber) needles
//   - Port (red) / starboard (green) close-hauled arc zones
//   - Centre disc: AWA label + signed angle value
//   - Bottom strip: AWS (left) | TWS (right) in knots
//
// Shares signalk_client with any other installed app — no duplicate WS connection.
// UI built by wind_gauge_build() from instrument_ui component.
// ---------------------------------------------------------------------------

class WindGaugeApp : public ESP_UI_PhoneApp {
public:
    WindGaugeApp();
    ~WindGaugeApp();

protected:
    bool run()    override;
    bool back()   override;
    bool pause()  override;
    bool resume() override;

private:
    WindGaugeHandles _wind;
    lv_timer_t      *_update_timer = nullptr;

    static void on_update_timer(lv_timer_t *t);
};
