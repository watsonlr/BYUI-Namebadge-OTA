idf_component_register(
    SRCS        "splash_screen.c"
    INCLUDE_DIRS "include" "."
    REQUIRES    driver esp_driver_spi
)
