#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"

void app_main(void)
{
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
