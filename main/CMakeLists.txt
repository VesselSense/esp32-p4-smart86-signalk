idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        signalk_client
        data_browser_app
        autopilot_app
        wind_rose_app
        wifi_settings_app
        wifi_manager
        settings_app
        unit_config
        signalk_auth
        audio_feedback
        esp_wifi
        nvs_flash
        esp_event
        mdns
)
