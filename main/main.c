#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "display.h"

static const char *TAG = "main";

void app_main(void)
{
    size_t psram_size = esp_psram_get_size();
    if (psram_size > 0) {
        ESP_LOGI(TAG, "PSRAM: %u bytes available", (unsigned)psram_size);
    } else {
        ESP_LOGW(TAG, "PSRAM: not available");
    }

    display_init();
    display_fill(DISPLAY_COLOR_BLACK);

    /* Mono font, scale 2, white on black */
    display_text_ctx_t mono = DISPLAY_CTX(DISPLAY_FONT_MONO, 2,
                                          DISPLAY_COLOR_WHITE,
                                          DISPLAY_COLOR_BLACK);
    display_print(&mono, 8, 20, "Mono: Hello World");

    /* Sans font, scale 2, yellow on black */
    display_text_ctx_t sans = DISPLAY_CTX(DISPLAY_FONT_SANS, 2,
                                          DISPLAY_COLOR_YELLOW,
                                          DISPLAY_COLOR_BLACK);
    display_print(&sans, 8, 60, "Sans: Hello World");

    /* Sans font, scale 3, cyan on black */
    display_text_ctx_t big = DISPLAY_CTX(DISPLAY_FONT_SANS, 3,
                                         DISPLAY_COLOR_CYAN,
                                         DISPLAY_COLOR_BLACK);
    display_print(&big, 8, 110, "Big Sans");

    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}
