idf_component_register(
    SRCS "wifi_settings_app.cpp" "wifi_icon.c"
    INCLUDE_DIRS "."
    REQUIRES
        wifi_manager
        espressif__esp-ui
        log
        esp_timer
)
