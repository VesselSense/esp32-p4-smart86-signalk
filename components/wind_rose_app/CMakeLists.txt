idf_component_register(
    SRCS "wind_rose_app.cpp" "wind_gauge_icon.c" "lv_font_montserrat_76.c" "lv_font_montserrat_62.c" "lv_font_montserrat_67.c" "lv_font_montserrat_bold_50.c"
    INCLUDE_DIRS "."
    REQUIRES
        signalk_client
        instrument_ui
        unit_config
        log
        esp_timer
)
