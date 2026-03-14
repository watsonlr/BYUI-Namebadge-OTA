#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"

#include "buttons.h"
#include "display.h"
#include "splash_screen.h"
#include "portal_mode.h"
#include "wifi_config.h"
#include "loader_menu.h"
#include "leds.h"

#define TAG "factory_loader"

/* ── Boot-time A+B detection ───────────────────────────────────────── */
/*
 * Poll A+B for up to LOADER_HOLD_MS before touching any other peripheral.
 * The check must complete within ~200 ms so normal student-app launches feel
 * instant.  Both buttons must be held simultaneously for the full window;
 * releasing either button aborts the check immediately (returns false).
 */
#define LOADER_HOLD_MS  150

/* ── Student-app launch ────────────────────────────────────────────── */
/*
 * Find the first OTA slot that contains a valid firmware image and boot into
 * it.  Returns false if no valid student app is installed (caller falls
 * through to loader mode).
 *
 * When this function finds a valid app it calls esp_restart() and never
 * returns.  The ESP-IDF second-stage bootloader then loads the OTA partition
 * directly.  To re-enter the factory loader the user holds A+B at power-on,
 * which is handled by the student app (a separate concern) or by performing
 * a "Reset to blank canvas" from this loader.
 */
static bool try_launch_student_app(void)
{
    /* Walk ota_0 → ota_1 → ota_2 (whichever partitions exist). */
    const esp_partition_subtype_t ota_subtypes[] = {
        ESP_PARTITION_SUBTYPE_APP_OTA_0,
        ESP_PARTITION_SUBTYPE_APP_OTA_1,
        ESP_PARTITION_SUBTYPE_APP_OTA_2,
    };

    /* Prefer the partition that otadata already says is active. */
    const esp_partition_t *preferred = esp_ota_get_boot_partition();
    if (preferred &&
        preferred->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(preferred, &desc) == ESP_OK) {
            ESP_LOGI(TAG, "Launching preferred OTA app: %s %s",
                     desc.project_name, desc.version);
            esp_ota_set_boot_partition(preferred);
            esp_restart();
            /* never reached */
        }
    }

    /* Fall back: scan all OTA slots for any valid image. */
    for (int i = 0; i < (int)(sizeof(ota_subtypes) / sizeof(ota_subtypes[0])); i++) {
        const esp_partition_t *part = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ota_subtypes[i], NULL);
        if (!part) continue;

        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(part, &desc) == ESP_OK) {
            ESP_LOGI(TAG, "Launching OTA app from slot %d: %s %s",
                     i, desc.project_name, desc.version);
            esp_ota_set_boot_partition(part);
            esp_restart();
            /* never reached */
        }
    }

    ESP_LOGW(TAG, "No valid student app found in OTA slots");
    return false;
}

/* ── Factory loader full initialisation ────────────────────────────── */

static void run_factory_loader(void)
{
    /* Log PSRAM availability (informational). */
    size_t psram = esp_psram_get_size();
    if (psram > 0) {
        ESP_LOGI(TAG, "PSRAM: %u bytes", (unsigned)psram);
    } else {
        ESP_LOGW(TAG, "PSRAM not available");
    }

    nvs_flash_init();
    display_init();
    leds_init();

    leds_clear();
    leds_show();

    splash_screen_run();

    /* Run WiFi configuration portal if the badge is not yet configured. */
    if (!wifi_config_is_configured()) {
        ESP_LOGI(TAG, "Not configured — launching portal");
        portal_mode_run(0);   /* blocks until user submits credentials */
    } else {
        ESP_LOGI(TAG, "Already configured — entering loader menu");
    }

    /* Hand off to the interactive loader menu. */
    loader_menu_run();

    /* loader_menu_run() loops forever; this point is never reached. */
}

/* ── Entry point ───────────────────────────────────────────────────── */

void app_main(void)
{
    /*
     * Stage 1 — quick button check (before any peripheral init).
     *
     * buttons_init() only configures GPIO — it is safe to call here.
     * If A+B are held for LOADER_HOLD_MS we enter the factory loader.
     * Otherwise we attempt to boot the installed student app immediately.
     */
    buttons_init();

    bool enter_loader = buttons_held(BTN_A | BTN_B, LOADER_HOLD_MS);

    if (!enter_loader) {
        /* Try to hand off to a student app.  Falls through if none exists. */
        if (try_launch_student_app()) {
            /* try_launch_student_app() only returns false — never true. */
        }
        /* No student app installed → fall into the loader automatically. */
        ESP_LOGI(TAG, "No student app — entering factory loader");
    } else {
        ESP_LOGI(TAG, "A+B held — entering factory loader");
    }

    /*
     * Stage 2 — factory loader.
     *
     * Reached when: A+B held at boot, OR no valid student app installed.
     */
    run_factory_loader();
}
