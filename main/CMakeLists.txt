idf_component_register(
    SRCS        "main.c"
    INCLUDE_DIRS "."
    REQUIRES    display splash_screen portal_mode ota_manager leds log esp_psram
)
