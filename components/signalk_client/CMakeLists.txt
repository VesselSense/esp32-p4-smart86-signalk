idf_component_register(
    SRCS "signalk_client.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_websocket_client
        json
        freertos
        esp_timer
        log
    PRIV_REQUIRES
        lvgl__lvgl
)
