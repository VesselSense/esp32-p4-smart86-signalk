idf_component_register(
    SRCS "signalk_auth.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_http_client
        nvs_flash
        json
        freertos
        log
)
