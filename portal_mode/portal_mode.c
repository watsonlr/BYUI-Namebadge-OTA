/**
 * @file portal_mode.c
 * @brief Wi-Fi config portal orchestrator.
 *
 * Combines wifi_config (AP + HTTP) with the display to show a QR code and
 * instructions.  Blocks until credentials are submitted or timeout expires.
 */

#include "portal_mode.h"
#include "wifi_config.h"
#include "display.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "portal_mode"

/* ── Layout constants (320×240 landscape) ───────────────────────── */

/* QR code: version 3 for "http://192.168.4.1/" ≈ 29 modules.
 * At 6 px/module → 174 px square, fits comfortably.             */
#define QR_MODULE_PX     6
#define QR_CX            (DISPLAY_W / 2)     /* 160 */
#define QR_CY            110                 /* vertical centre of QR */

/* Text rows below QR */
#define TEXT_Y_SSID      210
#define TEXT_Y_URL       224
#define TEXT_SCALE       1                   /* 8 px tall, scale 1 */

bool portal_mode_run(int timeout_s)
{
    /* ── Start portal ── */
    if (!wifi_config_start()) {
        ESP_LOGE(TAG, "Failed to start config portal");
        return false;
    }
    const char *url = wifi_config_url();  /* "http://192.168.4.1/" */
    ESP_LOGI(TAG, "Portal active — connect to SSID \"BADGE-CONFIG\", open %s", url);

    /* ── Draw UI ── */
    display_fill(DISPLAY_COLOR_WHITE);

    /* QR code (dark on white) */
    display_draw_qr(QR_CX, QR_CY, url,
                    QR_MODULE_PX,
                    DISPLAY_COLOR_BLACK,
                    DISPLAY_COLOR_WHITE);

    /* Instruction text */
    display_text_ctx_t ctx = DISPLAY_CTX(DISPLAY_FONT_SANS, TEXT_SCALE,
                                         DISPLAY_COLOR_BLACK,
                                         DISPLAY_COLOR_WHITE);
    display_print(&ctx,  4, TEXT_Y_SSID, "WiFi: BADGE-CONFIG  (open)");
    display_print(&ctx, 20, TEXT_Y_URL,  "http://192.168.4.1/");

    /* ── Poll until done or timeout ── */
    int elapsed = 0;
    const int poll_ms = 500;

    while (!wifi_config_done()) {
        vTaskDelay(pdMS_TO_TICKS(poll_ms));
        elapsed += poll_ms;
        if (timeout_s > 0 && elapsed >= timeout_s * 1000) {
            ESP_LOGW(TAG, "Portal timed out after %d s", timeout_s);
            wifi_config_stop();
            return false;
        }
    }

    ESP_LOGI(TAG, "Credentials saved — stopping portal");
    wifi_config_stop();

    /* Brief "saved" confirmation on screen */
    display_fill(DISPLAY_COLOR_BLACK);
    display_text_ctx_t ctx2 = DISPLAY_CTX(DISPLAY_FONT_SANS, 2,
                                           DISPLAY_COLOR_GREEN,
                                           DISPLAY_COLOR_BLACK);
    display_print(&ctx2, 60, 100, "Wi-Fi Saved!");
    vTaskDelay(pdMS_TO_TICKS(2000));

    return true;
}
