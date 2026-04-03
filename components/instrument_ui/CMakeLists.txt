idf_component_register(
    SRCS "instrument_helpers.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        signalk_client
        espressif__esp-ui
        unit_config
        log
        esp_timer
)
