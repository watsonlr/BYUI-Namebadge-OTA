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
#include <string.h>

#define TAG "portal_mode"

/* ── Layout constants (320×240 landscape) ───────────────────────── */

/* QR code: "WIFI:T:nopass;S:BYUI_NameBadge_XX;;" — built at runtime.
 * At 4 px/module + 4-module quiet zone → footprint 164 px square.
 *
 * All text scale 2 (16px tall, 16px wide per char, 20 chars max per line).
 *
 * Phase 1 layout (WiFi join QR):
 *   y=2      "Welcome to your"   scale 2 (16px) BLUE, centred
 *   y=20     "NameBadge!"        scale 2 (16px) BLUE, centred
 *   QR centre (160, 120) → footprint y=38..202
 *   y=206    "Scan to Join Your" scale 2 (16px) centred
 *   y=224    "Board's WiFi"      scale 2 (16px) centred
 *
 * Phase 2 layout (URL QR, after phone joined):
 *   y=20     "Device Connected!" scale 2 (16px) GREEN, centred
 *   QR same centre, footer text changes.                              */
#define QR_MODULE_PX   4
#define QR_CX          (DISPLAY_W / 2)     /* 160 */
#define QR_CY          120                 /* 38 top margin + 82 half-size */

#define HDR_Y1         2
#define HDR_Y2         20
#define TEXT_Y1        206
#define TEXT_Y2        224
#define HDR_SCALE      2    /* scale 2 = 16px — matches footer size */
#define TEXT_SCALE     2    /* scale 2 = 16px */


/* Centre a string horizontally for a given font scale. */
static int centre_x(const char *s, int scale)
{
    int px = (int)strlen(s) * DISPLAY_FONT_W * scale;
    int x  = (DISPLAY_W - px) / 2;
    return x < 0 ? 0 : x;
}

/* ── Helper: fill screen and draw QR + header + footer lines ─────── */
static void draw_portal_screen(const char *qr_payload,
                                uint16_t hdr_color,
                                const char *hdr1, const char *hdr2,
                                const char *foot1, const char *foot2)
{
    display_fill(DISPLAY_COLOR_WHITE);

    display_draw_qr(QR_CX, QR_CY, qr_payload,
                    QR_MODULE_PX,
                    DISPLAY_COLOR_BLACK,
                    DISPLAY_COLOR_WHITE);

    /* Header lines — caller-specified colour, scale 1, horizontally centred */
    display_text_ctx_t hctx = DISPLAY_CTX(DISPLAY_FONT_SANS, HDR_SCALE,
                                           hdr_color,
                                           DISPLAY_COLOR_WHITE);
    if (hdr1) display_print(&hctx, centre_x(hdr1, HDR_SCALE),  HDR_Y1, hdr1);
    if (hdr2) display_print(&hctx, centre_x(hdr2, HDR_SCALE),  HDR_Y2, hdr2);

    /* Footer lines — black, scale 2, horizontally centred */
    display_text_ctx_t fctx = DISPLAY_CTX(DISPLAY_FONT_SANS, TEXT_SCALE,
                                           DISPLAY_COLOR_BLACK,
                                           DISPLAY_COLOR_WHITE);
    if (foot1) display_print(&fctx, centre_x(foot1, TEXT_SCALE), TEXT_Y1, foot1);
    if (foot2) display_print(&fctx, centre_x(foot2, TEXT_SCALE), TEXT_Y2, foot2);
}

bool portal_mode_run(int timeout_s)
{
    /* ── Start portal ── */
    if (!wifi_config_start()) {
        ESP_LOGE(TAG, "Failed to start config portal");
        return false;
    }
    const char *url  = wifi_config_url();
    const char *ssid = wifi_config_ssid();
    ESP_LOGI(TAG, "Portal active — SSID=\"%s\"  URL=%s", ssid, url);

    /* Build WiFi QR payload with the unique SSID at runtime. */
    char wifi_qr[48];
    snprintf(wifi_qr, sizeof(wifi_qr), "WIFI:T:nopass;S:%s;;", ssid);

    /* ── Phase 1: invite user to scan and join WiFi ── */
    draw_portal_screen(wifi_qr, DISPLAY_COLOR_BLUE,
                       "Welcome to your",
                       "NameBadge!",
                       "Scan to Join Your",
                       "Board's WiFi");

    /* ── Poll until done or timeout ── */
    int elapsed = 0;
    const int poll_ms = 500;
    bool phase2_shown = false;

    while (!wifi_config_done()) {
        vTaskDelay(pdMS_TO_TICKS(poll_ms));
        elapsed += poll_ms;

        /* ── Phase 2: once phone has joined, swap QR to URL ── */
        if (!phase2_shown && wifi_config_sta_joined()) {
            phase2_shown = true;

            /* Build URL QR string (just the bare URL) */
            draw_portal_screen(url, DISPLAY_COLOR_GREEN,
                               NULL,
                               "Device Connected!",
                               "Scan to open browser",
                               "If not joined already");
        }

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
