idf_component_register(
    SRCS "signalk_app.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        signalk_client
        instrument_ui
        unit_config
        log
        esp_timer
)
