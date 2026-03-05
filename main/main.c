#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "splash_screen.h"

void app_main(void)
{
    splash_screen_run();
    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}
