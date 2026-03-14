idf_component_register(
    SRCS        "loader_menu.c"
    INCLUDE_DIRS "include"
    REQUIRES    display buttons portal_mode ota_manager log freertos esp_system
)
