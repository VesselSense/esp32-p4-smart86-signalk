#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    UNIT_CAT_TIMEZONE,
    UNIT_CAT_SPEED,
    UNIT_CAT_DEPTH,
    UNIT_CAT_TEMPERATURE,
    UNIT_CAT_DISTANCE,
    UNIT_CAT_PRESSURE,
    UNIT_CAT_VOLUME,
    UNIT_CAT_COUNT
} unit_category_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize — load preferences from NVS (call once at boot) */
void unit_config_init(void);

/* Get/set current option index for a category.
 * set() saves to NVS immediately. For UNIT_CAT_TIMEZONE, also calls setenv+tzset. */
int  unit_config_get(unit_category_t cat);
void unit_config_set(unit_category_t cat, int option);

/* Convert SI value to display value using current preference.
 * Not applicable to UNIT_CAT_TIMEZONE — returns 0. */
float unit_convert(unit_category_t cat, float si_value);

/* Label strings for current preference */
const char *unit_label(unit_category_t cat);        /* short: "KT", "M", "°C" */
const char *unit_label_long(unit_category_t cat);   /* long: "kts", "m", "°C" */

/* Display name for current option: "Knots", "US Eastern (ET)", etc. */
const char *unit_option_name(unit_category_t cat);

/* Enumeration for building picker UIs */
int         unit_option_count(unit_category_t cat);
const char *unit_option_name_at(unit_category_t cat, int idx);

/* Category display name: "Timezone", "Speed", etc. */
const char *unit_category_name(unit_category_t cat);

/* Timezone-specific: get POSIX TZ string for current selection */
const char *unit_config_get_tz_posix(void);

#ifdef __cplusplus
}
#endif
