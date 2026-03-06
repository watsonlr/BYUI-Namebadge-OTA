#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "display.h"
#include "splash_screen.h"
#include "portal_mode.h"
#include "ota_manager.h"

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
    splash_screen_run();

    /* Splash already holds 2 s; go straight into config portal. */
    portal_mode_run(0);  /* 0 = wait forever for the user to submit */

    /* Connect to the saved network, check manifest, update if newer. */
    ota_result_t ota_r = ota_manager_run();
    if (ota_r != OTA_RESULT_UP_TO_DATE && ota_r != OTA_RESULT_UPDATED) {
        ESP_LOGW(TAG, "OTA check failed: %d", (int)ota_r);
    }
    /* OTA_RESULT_UPDATED never reaches here — device already rebooted. */

    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}
