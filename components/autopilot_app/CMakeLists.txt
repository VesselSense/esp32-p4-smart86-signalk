idf_component_register(
    SRCS "autopilot_app.cpp" "lv_font_roboto_96.c" "helm_icon.c"
    INCLUDE_DIRS "."
    REQUIRES
        signalk_client
        signalk_auth
        instrument_ui
        unit_config
        esp_http_client
        audio_feedback
        log
        esp_timer
)
