idf_component_register(
    SRCS "audio_feedback.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        waveshare__esp32_p4_wifi6_touch_lcd_4b
        esp_codec_dev
        log
)
