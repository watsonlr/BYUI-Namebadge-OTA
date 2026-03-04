/**
 * @file main.c
 * @brief BYUI eBadge V3.0 — Starter Application
 *
 * This is the entry point for your eBadge project. It demonstrates
 * basic hardware bringup: NVS initialisation, GPIO configuration,
 * RGB LED blink, and button reading.
 *
 * Replace or extend app_main() with your own application logic.
 *
 * Hardware reference: HARDWARE.md
 * ─────────────────────────────────────────────────────────────────
 * Pin summary (most-used peripherals):
 *   Display  SPI2  CS=9  DC=13  RST=48  CLK=12  MOSI=11  MISO=10
 *   SD Card  SPI2  CS=3  (shared bus with display)
 *   Buttons  Up=17  Down=16  Left=14  Right=15  A=38  B=18
 *   RGB LED  R=6  G=5  B=4   (active-low or active-high — check schematic)
 *   WS2813B  Data=7  (RMT peripheral)
 *   Buzzer   GPIO=42 (PWM / toggle)
 *   IMU      I2C SDA=47  SCL=21
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_log.h"

/* ── GPIO definitions ───────────────────────────────────────────── */

/* RGB LED (check your board schematic for active-high vs active-low) */
#define LED_RED_GPIO    6
#define LED_GREEN_GPIO  5
#define LED_BLUE_GPIO   4

/* Buttons (active LOW with internal pull-up) */
#define BTN_UP_GPIO     17
#define BTN_DOWN_GPIO   16
#define BTN_LEFT_GPIO   14
#define BTN_RIGHT_GPIO  15
#define BTN_A_GPIO      38
#define BTN_B_GPIO      18

static const char *TAG = "ebadge";

/* ── Forward declarations ────────────────────────────────────────── */
static void gpio_init(void);
static void blink_rgb(int times, int delay_ms);

/* ── app_main ────────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "BYUI eBadge starter app booting...");

    /* Initialise NVS (required by Wi-Fi and other drivers) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Configure GPIOs */
    gpio_init();

    ESP_LOGI(TAG, "Hardware initialised. Starting main loop.");

    /* ── Your application code starts here ─────────────────────── */

    while (1) {
        /* Blink green LED three times on startup */
        blink_rgb(3, 200);

        /* Read button states and log them */
        bool btn_a = (gpio_get_level(BTN_A_GPIO) == 0);
        bool btn_b = (gpio_get_level(BTN_B_GPIO) == 0);
        bool btn_up    = (gpio_get_level(BTN_UP_GPIO) == 0);
        bool btn_down  = (gpio_get_level(BTN_DOWN_GPIO) == 0);
        bool btn_left  = (gpio_get_level(BTN_LEFT_GPIO) == 0);
        bool btn_right = (gpio_get_level(BTN_RIGHT_GPIO) == 0);

        if (btn_a || btn_b || btn_up || btn_down || btn_left || btn_right) {
            ESP_LOGI(TAG, "Buttons: A=%d B=%d U=%d D=%d L=%d R=%d",
                     btn_a, btn_b, btn_up, btn_down, btn_left, btn_right);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ── Helper implementations ─────────────────────────────────────── */

/**
 * @brief Configure GPIO pins for LEDs and buttons.
 */
static void gpio_init(void)
{
    /* RGB LED outputs */
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << LED_RED_GPIO) |
                        (1ULL << LED_GREEN_GPIO) |
                        (1ULL << LED_BLUE_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);

    /* Turn all LEDs off initially */
    gpio_set_level(LED_RED_GPIO,   0);
    gpio_set_level(LED_GREEN_GPIO, 0);
    gpio_set_level(LED_BLUE_GPIO,  0);

    /* Button inputs with internal pull-up */
    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << BTN_UP_GPIO)    |
                        (1ULL << BTN_DOWN_GPIO)  |
                        (1ULL << BTN_LEFT_GPIO)  |
                        (1ULL << BTN_RIGHT_GPIO) |
                        (1ULL << BTN_A_GPIO)     |
                        (1ULL << BTN_B_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_cfg);
}

/**
 * @brief Blink the green LED @p times with @p delay_ms on/off period.
 */
static void blink_rgb(int times, int delay_ms)
{
    for (int i = 0; i < times; i++) {
        gpio_set_level(LED_GREEN_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        gpio_set_level(LED_GREEN_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}
