idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        splash_screen
        driver
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_log
        led_strip
)
