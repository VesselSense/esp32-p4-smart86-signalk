#include "signalk_client.hpp"
#include "esp_timer.h"

/*
 * Stub signalk_client for the desktop simulator.
 * Returns hardcoded representative sample data.
 * All values in SI units (m/s, radians, etc.).
 */

static signalk_conn_state_t s_state = SK_STATE_CONNECTED;

bool signalk_client_is_connected(void)
{
    return s_state == SK_STATE_CONNECTED;
}

signalk_conn_state_t signalk_client_get_state(void)
{
    return s_state;
}

void signalk_client_set_state(signalk_conn_state_t state)
{
    s_state = state;
}

bool signalk_client_get_data(InstrumentStore *dest)
{
    if (!dest) return false;

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);

    dest->count = 0;  // reset before populating

    store_set(*dest, "navigation.speedThroughWater",               1.6462f, now);  /* 3.2 kn  */
    store_set(*dest, "navigation.headingMagnetic",                 3.7525f, now);  /* 215°    */
    store_set(*dest, "navigation.headingTrue",                     3.6652f, now);  /* 210°    */
    store_set(*dest, "environment.wind.speedApparent",             6.3277f, now);  /* AWS 12.3 kn */
    store_set(*dest, "environment.wind.angleApparent",             0.7854f, now);  /* AWA +45°    */
    store_set(*dest, "environment.wind.speedTrue",                 5.1440f, now);  /* TWS 10.0 kn */
    store_set(*dest, "environment.wind.angleTrueWater",            0.9599f, now);  /* TWA +55°    */
    store_set(*dest, "environment.depth.belowTransducer",         15.2f,    now);  /* metres  */
    store_set(*dest, "steering.rudderAngle",                      -0.0873f, now);  /* 5° port */
    store_set(*dest, "navigation.courseRhumbline.crossTrackError", -28.0f,   now);  /* 28m port */

    dest->mutex = nullptr;
    return true;
}

void signalk_client_start(const char *)  {}
void signalk_client_stop(void)   {}
bool signalk_client_needs_rediscovery(void) { return false; }
void signalk_client_subscribe(const char *, int) {}
void signalk_client_unsubscribe(const char *) {}
void signalk_client_watch(const char *, signalk_value_cb_t, void *) {}
