idf_component_register(
    SRCS "wind_gauge_app.cpp" "wind_gauge_icon.c"
    INCLUDE_DIRS "."
    REQUIRES
        signalk_client
        instrument_ui
        log
        esp_timer
)
