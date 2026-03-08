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
#define TEXT_Y1        188
#define TEXT_Y2        206
#define HDR_SCALE      2    /* scale 2 = 16px — matches footer size */
#define TEXT_SCALE     2    /* scale 2 = 16px */
/* Coloured bar: covers top of screen down to 4px below last header line */
#define HDR_BAR_H      (HDR_Y2 + HDR_SCALE * DISPLAY_FONT_H + 4)  /* = 40 */


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

    /* Coloured bar behind the header lines */
    display_fill_rect(0, 0, DISPLAY_W, HDR_BAR_H, hdr_color);

    display_draw_qr(QR_CX, QR_CY, qr_payload,
                    QR_MODULE_PX,
                    DISPLAY_COLOR_BLACK,
                    DISPLAY_COLOR_WHITE);

    /* Header lines — white text on the coloured bar */
    display_text_ctx_t hctx = DISPLAY_CTX(DISPLAY_FONT_SANS, HDR_SCALE,
                                           DISPLAY_COLOR_WHITE,
                                           hdr_color);
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
                       "ECEN NameBadge!",
                       "Scan to Join Your",
                       "Board's WiFi");

    /* ── Poll until done or timeout ── */
    int elapsed = 0;
    const int poll_ms = 500;
    bool phase2_shown = false;
    bool phase3_shown = false;

    while (!wifi_config_done()) {
        vTaskDelay(pdMS_TO_TICKS(poll_ms));
        elapsed += poll_ms;

        /* ── Phase 2: once phone has joined, swap QR to URL ── */
        if (!phase2_shown && wifi_config_sta_joined()) {
            phase2_shown = true;

            draw_portal_screen(url, DISPLAY_COLOR_GREEN,
                               "Device Connected!",
                               NULL,
                               "Scan to open browser",
                               "If not open yet.");



        /* ── Phase 3: browser has loaded the form — drop QR, show text ──
         * Requires Phase 2 has been visible for PHASE2_MIN_HOLD_MS so
         * the user has time to scan the URL QR if the OS probe beat
         * them to it.                                                  */
        } else if (!phase3_shown && phase2_shown && wifi_config_form_served()) {
            phase3_shown = true;

            /* White background, blue text — matches the web form style.
             * No QR needed — the browser is already open on their phone. */
            display_fill(DISPLAY_COLOR_WHITE);

            display_text_ctx_t ctx = DISPLAY_CTX(DISPLAY_FONT_SANS, 2,
                                                   DISPLAY_COLOR_BLUE,
                                                   DISPLAY_COLOR_WHITE);
            display_print(&ctx, centre_x("Fill out the form", 2),  88,
                          "Fill out the form");
            display_print(&ctx, centre_x("on your device", 2),    112,
                          "on your device");
            display_print(&ctx, centre_x("and save", 2),          136,
                          "and save");
        }

        if (timeout_s > 0 && elapsed >= timeout_s * 1000) {
            ESP_LOGW(TAG, "Portal timed out after %d s", timeout_s);
            wifi_config_stop();
            return false;
        }
    }

    ESP_LOGI(TAG, "Credentials saved — stopping portal");
    wifi_config_stop();

    /* ── Step 4: Welcome screen with the saved badge nickname ── */
    char nick[33] = {0};
    wifi_config_get_nick(nick, sizeof(nick));

    display_fill(DISPLAY_COLOR_BLACK);

    /* Green confirmation bar at the top */
    display_fill_rect(0, 0, DISPLAY_W, 40, DISPLAY_COLOR_GREEN);

    display_text_ctx_t hdr = DISPLAY_CTX(DISPLAY_FONT_SANS, 2,
                                          DISPLAY_COLOR_WHITE,
                                          DISPLAY_COLOR_GREEN);
    display_print(&hdr, centre_x("Setup Complete!", 2), 4,  "Setup Complete!");
    display_print(&hdr, centre_x("Welcome to your", 2), 22, "Welcome to your");

    display_text_ctx_t lbl = DISPLAY_CTX(DISPLAY_FONT_SANS, 2,
                                          DISPLAY_COLOR_YELLOW,
                                          DISPLAY_COLOR_BLACK);
    display_print(&lbl, centre_x("eBadge!", 2), 52, "eBadge!");

    /* Badge nickname — scale 3 if it fits, else scale 2 */
    int name_scale = (strlen(nick) * DISPLAY_FONT_W * 3 <= DISPLAY_W) ? 3 : 2;
    display_text_ctx_t name_ctx = DISPLAY_CTX(DISPLAY_FONT_SANS, name_scale,
                                               DISPLAY_COLOR_WHITE,
                                               DISPLAY_COLOR_BLACK);
    display_print(&name_ctx,
                  centre_x(nick, name_scale),
                  90, nick);

    display_text_ctx_t info = DISPLAY_CTX(DISPLAY_FONT_SANS, 1,
                                           DISPLAY_COLOR_CYAN,
                                           DISPLAY_COLOR_BLACK);
    display_print(&info, centre_x("Connecting to Wi-Fi...", 1), 150,
                  "Connecting to Wi-Fi...");
    display_print(&info, centre_x("Checking for updates.", 1), 164,
                  "Checking for updates.");
    display_print(&info, centre_x("This may take a moment.", 1), 178,
                  "This may take a moment.");

    vTaskDelay(pdMS_TO_TICKS(3000));

    return true;
}
