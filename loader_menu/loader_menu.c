#include "loader_menu.h"

#include "display.h"
#include "buttons.h"
#include "portal_mode.h"
#include "ota_manager.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdio.h>

#define TAG  "loader_menu"

/* ── Palette ───────────────────────────────────────────────────────── */
/* BYU navy blue (approx #002E78) */
#define COLOR_BYUI_BLUE   DISPLAY_RGB565(  0,  46, 120)
/* Slightly lighter blue for header text contrast */
#define COLOR_HEADER_BG   DISPLAY_RGB565(  0,  36,  96)
/* Dark text on white rows */
#define COLOR_DARK        DISPLAY_RGB565( 30,  30,  30)
/* Accent yellow for the selection arrow */
#define COLOR_YELLOW      DISPLAY_RGB565(255, 200,   0)
/* Footer bar */
#define COLOR_FOOTER_BG   DISPLAY_RGB565( 24,  24,  24)

/* ── Layout constants (320 × 240 landscape) ────────────────────────── */
#define HEADER_Y      0
#define HEADER_H     30
#define ITEMS_START  32          /* first pixel below header */
#define ITEM_H       36          /* 5 × 36 = 180 px          */
#define FOOTER_Y    212
#define FOOTER_H     28

#define ITEM_TEXT_SCALE  2                          /* 16 × 16 px per glyph */
#define ITEM_CHAR_W     (DISPLAY_FONT_W * ITEM_TEXT_SCALE)
#define ITEM_ARROW_X     8
#define ITEM_TEXT_X     32

#define NUM_ITEMS  5

/* ── Menu items ────────────────────────────────────────────────────── */
static const char *ITEM_LABELS[NUM_ITEMS] = {
    "OTA App Download",
    "Load from SD Card",
    "Configure WiFi",
    "Bare-metal / Flash",
    "Update SD Recovery",
};

/* ── Helpers ───────────────────────────────────────────────────────── */

static int item_y(int idx)
{
    return ITEMS_START + idx * ITEM_H;
}

static void draw_header(void)
{
    display_fill_rect(0, HEADER_Y, DISPLAY_W, HEADER_H, COLOR_HEADER_BG);

    const char *title = "BYUI Badge Loader";
    int title_w = (int)strlen(title) * DISPLAY_FONT_W * 2;
    int title_x = (DISPLAY_W - title_w) / 2;
    display_draw_string(title_x, HEADER_Y + 7, title,
                        DISPLAY_COLOR_WHITE, COLOR_HEADER_BG, 2);
}

static void draw_footer(void)
{
    display_fill_rect(0, FOOTER_Y, DISPLAY_W, FOOTER_H, COLOR_FOOTER_BG);

    const char *hint = "Up/Dn:move   A/Rt:select";
    int hint_w = (int)strlen(hint) * DISPLAY_FONT_W;  /* scale 1 */
    int hint_x = (DISPLAY_W - hint_w) / 2;
    display_draw_string(hint_x, FOOTER_Y + 8, hint,
                        DISPLAY_COLOR_WHITE, COLOR_FOOTER_BG, 1);
}

static void draw_item(int idx, bool selected)
{
    int y = item_y(idx);

    uint16_t bg = selected ? COLOR_BYUI_BLUE : DISPLAY_COLOR_WHITE;
    uint16_t fg = selected ? DISPLAY_COLOR_WHITE : COLOR_DARK;

    display_fill_rect(0, y, DISPLAY_W, ITEM_H, bg);

    /* Selection arrow */
    if (selected) {
        display_draw_string(ITEM_ARROW_X, y + 10, ">",
                            COLOR_YELLOW, bg, ITEM_TEXT_SCALE);
    }

    /* Number + label */
    char buf[32];
    snprintf(buf, sizeof(buf), "%d. %s", idx + 1, ITEM_LABELS[idx]);
    display_draw_string(ITEM_TEXT_X, y + 10, buf, fg, bg, ITEM_TEXT_SCALE);
}

static void draw_menu(int selected)
{
    draw_header();
    for (int i = 0; i < NUM_ITEMS; i++) {
        draw_item(i, i == selected);
    }
    draw_footer();
}

/* ── Info / stub screens ───────────────────────────────────────────── */

static void wait_any_button(void)
{
    buttons_wait_press(0);   /* wait indefinitely */
}

static void show_info_screen(const char *title,
                             const char *lines[],
                             int         num_lines)
{
    display_fill(DISPLAY_COLOR_BLACK);

    /* Title bar */
    display_fill_rect(0, 0, DISPLAY_W, 28, COLOR_BYUI_BLUE);
    int tw = (int)strlen(title) * DISPLAY_FONT_W * 2;
    display_draw_string((DISPLAY_W - tw) / 2, 6, title,
                        DISPLAY_COLOR_WHITE, COLOR_BYUI_BLUE, 2);

    /* Body lines */
    for (int i = 0; i < num_lines; i++) {
        display_draw_string(12, 44 + i * 22, lines[i],
                            DISPLAY_COLOR_WHITE, DISPLAY_COLOR_BLACK, 1);
    }

    /* Footer */
    display_fill_rect(0, FOOTER_Y, DISPLAY_W, FOOTER_H, COLOR_FOOTER_BG);
    const char *hint = "Press any button to return";
    int hw = (int)strlen(hint) * DISPLAY_FONT_W;
    display_draw_string((DISPLAY_W - hw) / 2, FOOTER_Y + 8, hint,
                        DISPLAY_COLOR_WHITE, COLOR_FOOTER_BG, 1);

    wait_any_button();
}

/* ── Action: OTA download ──────────────────────────────────────────── */

static void action_ota_download(void)
{
    /* Show status screen while OTA runs */
    display_fill(DISPLAY_COLOR_BLACK);
    display_fill_rect(0, 0, DISPLAY_W, 28, COLOR_BYUI_BLUE);
    display_draw_string(60, 6, "OTA App Download",
                        DISPLAY_COLOR_WHITE, COLOR_BYUI_BLUE, 2);

    display_draw_string(12, 60,  "Connecting to WiFi...",
                        DISPLAY_COLOR_WHITE, DISPLAY_COLOR_BLACK, 1);
    display_draw_string(12, 82,  "Checking manifest...",
                        DISPLAY_COLOR_WHITE, DISPLAY_COLOR_BLACK, 1);
    display_draw_string(12, 104, "This may take a moment.",
                        DISPLAY_COLOR_WHITE, DISPLAY_COLOR_BLACK, 1);

    ota_result_t result = ota_manager_run();
    /* OTA_RESULT_UPDATED never returns — device reboots automatically. */

    /* Show result for any non-reboot outcome */
    display_fill_rect(0, 140, DISPLAY_W, 60, DISPLAY_COLOR_BLACK);

    const char *msg;
    uint16_t    msg_color;

    switch (result) {
    case OTA_RESULT_UP_TO_DATE:
        msg       = "App is up to date.";
        msg_color = DISPLAY_COLOR_GREEN;
        break;
    case OTA_RESULT_NO_WIFI:
        msg       = "WiFi connect failed.";
        msg_color = DISPLAY_COLOR_RED;
        break;
    case OTA_RESULT_NO_MANIFEST:
        msg       = "Manifest not found.";
        msg_color = DISPLAY_COLOR_RED;
        break;
    case OTA_RESULT_DOWNLOAD_FAIL:
        msg       = "Download failed.";
        msg_color = DISPLAY_COLOR_RED;
        break;
    case OTA_RESULT_VERIFY_FAIL:
        msg       = "Verify failed.";
        msg_color = DISPLAY_COLOR_RED;
        break;
    case OTA_RESULT_FLASH_FAIL:
        msg       = "Flash write failed.";
        msg_color = DISPLAY_COLOR_RED;
        break;
    default:
        msg       = "Unknown result.";
        msg_color = DISPLAY_COLOR_YELLOW;
        break;
    }

    int mw = (int)strlen(msg) * DISPLAY_FONT_W * 2;
    display_draw_string((DISPLAY_W - mw) / 2, 150, msg,
                        msg_color, DISPLAY_COLOR_BLACK, 2);

    display_fill_rect(0, FOOTER_Y, DISPLAY_W, FOOTER_H, COLOR_FOOTER_BG);
    const char *hint = "Press any button to return";
    int hw = (int)strlen(hint) * DISPLAY_FONT_W;
    display_draw_string((DISPLAY_W - hw) / 2, FOOTER_Y + 8, hint,
                        DISPLAY_COLOR_WHITE, COLOR_FOOTER_BG, 1);

    wait_any_button();
}

/* ── Action: Load from SD Card (stub) ─────────────────────────────── */

static void action_sd_load(void)
{
    const char *lines[] = {
        "SD card app loading is",
        "coming in a future update.",
        "",
        "To install apps now, use",
        "option 1 (OTA Download).",
    };
    show_info_screen("Load from SD Card",
                     lines, sizeof(lines) / sizeof(lines[0]));
}

/* ── Action: Configure WiFi ────────────────────────────────────────── */

static void action_configure_wifi(void)
{
    portal_mode_run(0);   /* blocks until user submits credentials */
}

/* ── Action: Bare-metal / serial flash mode ────────────────────────── */

static void action_bare_metal(void)
{
    const char *lines[] = {
        "To enter serial flash mode:",
        "",
        "  1. Hold IO0 (BOOT button)",
        "  2. Press then release RESET",
        "  3. Release IO0",
        "",
        "Then flash from your PC:",
        "  idf.py -p <PORT> flash",
        "  -- or --",
        "  esptool.py write_flash ...",
    };
    show_info_screen("Bare-metal / Flash Mode",
                     lines, sizeof(lines) / sizeof(lines[0]));
    /* Holding BOOT+RESET is handled by ROM — nothing more to do here. */
}

/* ── Action: Update SD recovery (stub) ────────────────────────────── */

static void action_sd_recovery(void)
{
    const char *lines[] = {
        "SD recovery image creation",
        "is coming in a future update.",
        "",
        "For now, flash the recovery",
        "image manually via USB using",
        "esptool.py merge_bin.",
    };
    show_info_screen("Update SD Recovery",
                     lines, sizeof(lines) / sizeof(lines[0]));
}

/* ── Public entry point ────────────────────────────────────────────── */

void loader_menu_run(void)
{
    int selection = 0;

    draw_menu(selection);

    for (;;) {
        button_t btn = buttons_wait_press(0);

        if (btn & (BTN_UP | BTN_LEFT)) {
            selection = (selection - 1 + NUM_ITEMS) % NUM_ITEMS;
            draw_menu(selection);
            continue;
        }

        if (btn & BTN_DOWN) {
            selection = (selection + 1) % NUM_ITEMS;
            draw_menu(selection);
            continue;
        }

        if (btn & (BTN_A | BTN_RIGHT)) {
            ESP_LOGI(TAG, "Selected item %d: %s",
                     selection + 1, ITEM_LABELS[selection]);

            switch (selection) {
            case 0:  action_ota_download();   break;
            case 1:  action_sd_load();        break;
            case 2:  action_configure_wifi(); break;
            case 3:  action_bare_metal();     break;
            case 4:  action_sd_recovery();    break;
            default: break;
            }

            /* Redraw menu after returning from any action */
            draw_menu(selection);
        }
        /* BTN_B ignored at top level (no parent menu to go back to) */
    }
}
