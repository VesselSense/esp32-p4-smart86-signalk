#include "unit_config.hpp"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifndef SIMULATOR_BUILD
#include "nvs_flash.h"
#include "nvs.h"
#endif

static const char *TAG = "unit_cfg";

// ---------------------------------------------------------------------------
// Option definition
// ---------------------------------------------------------------------------

struct UnitOption {
    const char *name;        // display name: "Knots", "Meters", etc.
    const char *label;       // short label: "KT", "M", etc.
    const char *label_long;  // long label: "kts", "m", etc.
    float       factor;      // multiply SI value by this
    float       offset;      // then add this (for temperature)
    const char *extra;       // timezone: POSIX string; others: NULL
};

struct CategoryDef {
    const char       *name;       // "Timezone", "Speed", etc.
    const char       *nvs_key;    // NVS key: "tz", "spd", etc.
    const UnitOption *options;
    int               count;
    int               default_idx;
};

// ---------------------------------------------------------------------------
// Timezone options
// ---------------------------------------------------------------------------

static const UnitOption s_tz_options[] = {
    { "UTC",                  "", "", 0, 0, "UTC0" },
    { "US Eastern (ET)",      "", "", 0, 0, "EST5EDT,M3.2.0,M11.1.0" },
    { "US Central (CT)",      "", "", 0, 0, "CST6CDT,M3.2.0,M11.1.0" },
    { "US Mountain (MT)",     "", "", 0, 0, "MST7MDT,M3.2.0,M11.1.0" },
    { "US Pacific (PT)",      "", "", 0, 0, "PST8PDT,M3.2.0,M11.1.0" },
    { "US Alaska (AKT)",      "", "", 0, 0, "AKST9AKDT,M3.2.0,M11.1.0" },
    { "US Hawaii (HST)",      "", "", 0, 0, "HST10" },
    { "Atlantic (AST)",       "", "", 0, 0, "AST4ADT,M3.2.0,M11.1.0" },
    { "Newfoundland (NST)",   "", "", 0, 0, "NST3:30NDT,M3.2.0/0:01,M11.1.0/0:01" },
    { "UK / Ireland (GMT)",   "", "", 0, 0, "GMT0BST,M3.5.0/1,M10.5.0" },
    { "Central Europe (CET)", "", "", 0, 0, "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Eastern Europe (EET)", "", "", 0, 0, "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Australia Eastern",    "", "", 0, 0, "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "Australia Central",    "", "", 0, 0, "ACST-9:30ACDT,M10.1.0,M4.1.0/3" },
    { "Australia Western",    "", "", 0, 0, "AWST-8" },
    { "New Zealand",          "", "", 0, 0, "NZST-12NZDT,M9.5.0,M4.1.0/3" },
    { "Japan (JST)",          "", "", 0, 0, "JST-9" },
    { "China (CST)",          "", "", 0, 0, "CST-8" },
    { "India (IST)",          "", "", 0, 0, "IST-5:30" },
    { "Dubai (GST)",          "", "", 0, 0, "GST-4" },
    { "Caribbean (AST)",      "", "", 0, 0, "AST4" },
    { "Argentina (ART)",      "", "", 0, 0, "ART3" },
    { "South Africa (SAST)",  "", "", 0, 0, "SAST-2" },
};

// ---------------------------------------------------------------------------
// Speed options (from m/s)
// ---------------------------------------------------------------------------

static const UnitOption s_speed_options[] = {
    { "Knots",  "KT",   "kts",  1.94384f,  0, NULL },
    { "km/h",   "km/h", "km/h", 3.6f,      0, NULL },
    { "mph",    "mph",  "mph",  2.23694f,   0, NULL },
    { "m/s",    "m/s",  "m/s",  1.0f,       0, NULL },
};

// ---------------------------------------------------------------------------
// Depth options (from meters)
// ---------------------------------------------------------------------------

static const UnitOption s_depth_options[] = {
    { "Meters",  "M",  "m",  1.0f,      0, NULL },
    { "Feet",    "FT", "ft", 3.28084f,  0, NULL },
    { "Fathoms", "FM", "fm", 0.546807f, 0, NULL },
};

// ---------------------------------------------------------------------------
// Temperature options (from Kelvin)
// ---------------------------------------------------------------------------

static const UnitOption s_temp_options[] = {
    { "Celsius",    "\xc2\xb0""C", "\xc2\xb0""C", 1.0f,     -273.15f, NULL },
    { "Fahrenheit", "\xc2\xb0""F", "\xc2\xb0""F", 1.8f,     -459.67f, NULL },
};

// ---------------------------------------------------------------------------
// Distance options (from meters)
// ---------------------------------------------------------------------------

static const UnitOption s_dist_options[] = {
    { "Nautical Miles", "NM", "nm",  1.0f / 1852.0f,    0, NULL },
    { "Miles",          "MI", "mi",  1.0f / 1609.344f,  0, NULL },
    { "Kilometers",     "KM", "km",  1.0f / 1000.0f,    0, NULL },
};

// ---------------------------------------------------------------------------
// Pressure options (from Pascals)
// ---------------------------------------------------------------------------

static const UnitOption s_pressure_options[] = {
    { "hPa",  "hPa",  "hPa",  0.01f,       0, NULL },
    { "mbar", "mbar", "mbar", 0.01f,        0, NULL },
    { "inHg", "inHg", "inHg", 0.00029530f,  0, NULL },
};

// ---------------------------------------------------------------------------
// Volume options (from m³)
// ---------------------------------------------------------------------------

static const UnitOption s_volume_options[] = {
    { "Liters",  "L",   "liters",  1000.0f,   0, NULL },
    { "Gallons", "gal", "gallons", 264.172f,   0, NULL },
};

// ---------------------------------------------------------------------------
// Category table
// ---------------------------------------------------------------------------

static const CategoryDef s_categories[UNIT_CAT_COUNT] = {
    [UNIT_CAT_TIMEZONE]    = { "Timezone",    "tz",  s_tz_options,       sizeof(s_tz_options)/sizeof(s_tz_options[0]),       0 },
    [UNIT_CAT_SPEED]       = { "Speed",       "spd", s_speed_options,    sizeof(s_speed_options)/sizeof(s_speed_options[0]), 0 },
    [UNIT_CAT_DEPTH]       = { "Depth",       "dep", s_depth_options,    sizeof(s_depth_options)/sizeof(s_depth_options[0]), 0 },
    [UNIT_CAT_TEMPERATURE] = { "Temperature", "tmp", s_temp_options,     sizeof(s_temp_options)/sizeof(s_temp_options[0]),   0 },
    [UNIT_CAT_DISTANCE]    = { "Distance",    "dst", s_dist_options,     sizeof(s_dist_options)/sizeof(s_dist_options[0]),   0 },
    [UNIT_CAT_PRESSURE]    = { "Pressure",    "prs", s_pressure_options, sizeof(s_pressure_options)/sizeof(s_pressure_options[0]), 0 },
    [UNIT_CAT_VOLUME]      = { "Volume",      "vol", s_volume_options,   sizeof(s_volume_options)/sizeof(s_volume_options[0]),     0 },
};

// ---------------------------------------------------------------------------
// Cached preferences
// ---------------------------------------------------------------------------

static int s_pref[UNIT_CAT_COUNT] = {};

// ---------------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------------

static const char *NVS_NS = "unit_cfg";

#ifndef SIMULATOR_BUILD
static void load_from_nvs(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) != ESP_OK) return;

    for (int c = 0; c < UNIT_CAT_COUNT; c++) {
        int8_t val = 0;
        if (nvs_get_i8(nvs, s_categories[c].nvs_key, &val) == ESP_OK) {
            if (val >= 0 && val < s_categories[c].count) {
                s_pref[c] = val;
            }
        }
    }
    nvs_close(nvs);
}

static void save_to_nvs(unit_category_t cat)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_i8(nvs, s_categories[cat].nvs_key, (int8_t)s_pref[cat]);
    nvs_commit(nvs);
    nvs_close(nvs);
}
#endif

// ---------------------------------------------------------------------------
// Apply timezone
// ---------------------------------------------------------------------------

static void apply_timezone(int idx)
{
    const char *posix = s_tz_options[idx].extra;
    if (posix && posix[0]) {
        setenv("TZ", posix, 1);
        tzset();
        ESP_LOGI(TAG, "Timezone applied: %s (%s)", s_tz_options[idx].name, posix);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void unit_config_init(void)
{
    /* Set defaults */
    for (int c = 0; c < UNIT_CAT_COUNT; c++) {
        s_pref[c] = s_categories[c].default_idx;
    }

#ifndef SIMULATOR_BUILD
    load_from_nvs();
#endif

    /* Apply timezone on boot */
    apply_timezone(s_pref[UNIT_CAT_TIMEZONE]);

    ESP_LOGI(TAG, "Unit config loaded: tz=%d spd=%d dep=%d tmp=%d dst=%d",
             s_pref[UNIT_CAT_TIMEZONE], s_pref[UNIT_CAT_SPEED],
             s_pref[UNIT_CAT_DEPTH], s_pref[UNIT_CAT_TEMPERATURE],
             s_pref[UNIT_CAT_DISTANCE]);
}

int unit_config_get(unit_category_t cat)
{
    if (cat < 0 || cat >= UNIT_CAT_COUNT) return 0;
    return s_pref[cat];
}

void unit_config_set(unit_category_t cat, int option)
{
    if (cat < 0 || cat >= UNIT_CAT_COUNT) return;
    if (option < 0 || option >= s_categories[cat].count) return;

    s_pref[cat] = option;

#ifndef SIMULATOR_BUILD
    save_to_nvs(cat);
#endif

    if (cat == UNIT_CAT_TIMEZONE) {
        apply_timezone(option);
    }

    ESP_LOGI(TAG, "Set %s = %s (idx %d)", s_categories[cat].name,
             s_categories[cat].options[option].name, option);
}

float unit_convert(unit_category_t cat, float si_value)
{
    if (cat < 0 || cat >= UNIT_CAT_COUNT) return si_value;
    if (cat == UNIT_CAT_TIMEZONE) return 0;  /* not a numeric conversion */

    const UnitOption *opt = &s_categories[cat].options[s_pref[cat]];
    return si_value * opt->factor + opt->offset;
}

const char *unit_label(unit_category_t cat)
{
    if (cat < 0 || cat >= UNIT_CAT_COUNT) return "";
    return s_categories[cat].options[s_pref[cat]].label;
}

const char *unit_label_long(unit_category_t cat)
{
    if (cat < 0 || cat >= UNIT_CAT_COUNT) return "";
    return s_categories[cat].options[s_pref[cat]].label_long;
}

const char *unit_option_name(unit_category_t cat)
{
    if (cat < 0 || cat >= UNIT_CAT_COUNT) return "";
    return s_categories[cat].options[s_pref[cat]].name;
}

int unit_option_count(unit_category_t cat)
{
    if (cat < 0 || cat >= UNIT_CAT_COUNT) return 0;
    return s_categories[cat].count;
}

const char *unit_option_name_at(unit_category_t cat, int idx)
{
    if (cat < 0 || cat >= UNIT_CAT_COUNT) return "";
    if (idx < 0 || idx >= s_categories[cat].count) return "";
    return s_categories[cat].options[idx].name;
}

const char *unit_category_name(unit_category_t cat)
{
    if (cat < 0 || cat >= UNIT_CAT_COUNT) return "";
    return s_categories[cat].name;
}

const char *unit_config_get_tz_posix(void)
{
    return s_tz_options[s_pref[UNIT_CAT_TIMEZONE]].extra;
}
