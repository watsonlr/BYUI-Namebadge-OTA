#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "display.h"

static const char *TAG = "main";

/* Screen: 320×240 landscape.  Font: 8×8 at scale 3 → 24×24 px/char.
 * "Hello World" = 11 chars × 24 = 264 px wide, 24 px tall.
 * Centred: x = (320-264)/2 = 28,  y = (240-24)/2 = 108.          */
#define TEXT_STR    "Hello World"
#define TEXT_SCALE  3
#define TEXT_X      28
#define TEXT_Y      108

void app_main(void)
{
    ESP_LOGI(TAG, "Booting — Hello World on TFT");

    display_init();
    display_fill(DISPLAY_COLOR_BLACK);
    display_draw_string(TEXT_X, TEXT_Y, TEXT_STR,
                        DISPLAY_COLOR_WHITE, DISPLAY_COLOR_BLACK,
                        TEXT_SCALE);

    ESP_LOGI(TAG, "\"" TEXT_STR "\" displayed at scale %d", TEXT_SCALE);

    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}
