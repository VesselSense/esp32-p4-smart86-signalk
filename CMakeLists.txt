cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "components/signalk_client"
    "components/instrument_ui"
    "components/data_browser_app"
    "components/autopilot_app"
    "components/wind_rose_app"
    "components/signalk_auth"
    "components/audio_feedback"
    "components/wifi_manager"
    "components/wifi_settings_app"
    "components/settings_app"
    "components/unit_config"
)

set(EXCLUDE_COMPONENTS "wind_gauge_app" "signalk_app")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(waveshare_p4_signalk)
