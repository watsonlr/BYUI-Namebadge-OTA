#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "display.h"
#include "splash_screen.h"
#include "portal_mode.h"
#include "ota_manager.h"
#include "wifi_config.h"

#include <stdio.h>
#include <string.h>

#define TAG "main"

#define BTN_A_GPIO  38
#define BTN_B_GPIO  18
/* Both buttons must be held this long to trigger a factory reset. */
#define RESET_HOLD_MS   2000
#define RESET_POLL_MS   50

/* Show a simple welcome screen with the saved badge nickname. */
static void show_welcome(const char *nick)
{
    display_fill(DISPLAY_COLOR_WHITE);

    display_text_ctx_t ctx_lbl = DISPLAY_CTX(DISPLAY_FONT_SANS, 2,
                                              DISPLAY_COLOR_BLUE,
                                              DISPLAY_COLOR_WHITE);
    display_text_ctx_t ctx_name = DISPLAY_CTX(DISPLAY_FONT_SANS, 3,
                                               DISPLAY_COLOR_BLACK,
                                               DISPLAY_COLOR_WHITE);

    /* "Welcome:" centred at ~y=90 */
    const char *lbl = "Welcome:";
    int lbl_x = (DISPLAY_W - (int)strlen(lbl) * DISPLAY_FONT_W * 2) / 2;
    display_print(&ctx_lbl, lbl_x, 90, lbl);

    /* Badge nickname centred below, scale 3 (24 px tall) */
    int name_w = (int)strlen(nick) * DISPLAY_FONT_W * 3;
    int name_x = (DISPLAY_W - name_w) / 2;
    if (name_x < 0) name_x = 0;
    display_print(&ctx_name, name_x, 118, nick);
}

/*
 * Poll for A+B held simultaneously for RESET_HOLD_MS.
 * Returns true if the combo was detected; false if the window expired.
 */
static bool wait_for_reset_combo(int window_ms)
{
    /* Configure both pins as inputs with internal pull-ups. */
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BTN_A_GPIO) | (1ULL << BTN_B_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    int held_ms    = 0;
    int elapsed_ms = 0;

    while (elapsed_ms < window_ms) {
        bool a_pressed = (gpio_get_level(BTN_A_GPIO) == 0);
        bool b_pressed = (gpio_get_level(BTN_B_GPIO) == 0);

        if (a_pressed && b_pressed) {
            held_ms += RESET_POLL_MS;
            if (held_ms >= RESET_HOLD_MS) {
                return true;
            }
        } else {
            held_ms = 0;   /* reset counter if either button released */
        }

        vTaskDelay(pdMS_TO_TICKS(RESET_POLL_MS));
        elapsed_ms += RESET_POLL_MS;
    }
    return false;
}

void app_main(void)
{
    size_t psram_size = esp_psram_get_size();
    if (psram_size > 0) {
        ESP_LOGI(TAG, "PSRAM: %u bytes available", (unsigned)psram_size);
    } else {
        ESP_LOGW(TAG, "PSRAM: not available");
    }

    nvs_flash_init();

    display_init();
    splash_screen_run();

    if (wifi_config_is_configured()) {
        /* Badge already set up — read nick and show welcome screen. */
        char nick[33] = {0};
        wifi_config_get_nick(nick, sizeof(nick));
        ESP_LOGI(TAG, "Already configured — nick='%s', skipping portal", nick);
        show_welcome(nick);

        /* Give the user a 5-second window to hold A+B for a factory reset. */
        display_text_ctx_t hint = DISPLAY_CTX(DISPLAY_FONT_SANS, 1,
                                               DISPLAY_COLOR_BLUE,
                                               DISPLAY_COLOR_WHITE);
        const char *hint_str = "Press A & B to reconfig";
        int hx = (DISPLAY_W - (int)strlen(hint_str) * DISPLAY_FONT_W) / 2;
        display_print(&hint, hx, 220, hint_str);

        if (wait_for_reset_combo(5000)) {
            ESP_LOGW(TAG, "A+B held — erasing user config and rebooting");

            display_fill(DISPLAY_COLOR_BLACK);
            display_text_ctx_t ctx = DISPLAY_CTX(DISPLAY_FONT_SANS, 2,
                                                  DISPLAY_COLOR_RED,
                                                  DISPLAY_COLOR_BLACK);
            display_print(&ctx, 44, 104, "Config erased.");
            display_print(&ctx, 68, 124, "Rebooting...");
            vTaskDelay(pdMS_TO_TICKS(1500));

            nvs_flash_erase_partition(WIFI_CONFIG_NVS_PARTITION);
            esp_restart();
        }
    } else {
        /* First boot (or data erased) — run the configuration portal. */
        portal_mode_run(0);  /* 0 = wait forever for the user to submit */
    }

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
