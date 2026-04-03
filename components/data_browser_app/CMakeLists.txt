idf_component_register(
    SRCS "data_browser_app.cpp" "lv_font_montserrat_90.c" "lv_font_montserrat_120.c" "lv_font_montserrat_310.c" "lv_font_montserrat_360.c"
    INCLUDE_DIRS "."
    REQUIRES
        signalk_client
        signalk_auth
        unit_config
        espressif__esp-ui
        esp_http_client
        log
        esp_timer
)
