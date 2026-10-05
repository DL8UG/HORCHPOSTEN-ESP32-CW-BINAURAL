idf_component_register(
    SRCS "main.c" "board.c" "codec.c" "codec_es8388.c" "codec_ac101.c"
         "audio_io.c" "dsp.c" "pitch.c" "buttons.c" "settings.c" "led.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_i2s esp_driver_i2c esp_driver_gpio nvs_flash esp_app_format esp_timer
)
