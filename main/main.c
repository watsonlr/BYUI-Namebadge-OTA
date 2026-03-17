#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
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
static void try_launch_student_app(void)
{
    /* Only auto-boot if otadata explicitly chose an OTA partition.
     * If it points to factory (or was erased by the A+B bootloader hook),
     * respected that intent and stay in the loader. */
    const esp_partition_t *preferred = esp_ota_get_boot_partition();
    if (!preferred || preferred->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY)
        return;

    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(preferred, &desc) != ESP_OK)
        return;  /* OTA slot has no valid image */

    ESP_LOGI(TAG, "Booting student app: %s %s",
             desc.project_name, desc.version);
    esp_restart();  /* bootloader reads otadata → boots OTA partition */
}

/* ── Factory loader full initialisation ────────────────────────────── */

static void run_factory_loader(void)
{
    /* Init buttons FIRST — before display_init/leds_init/nvs_flash_init —
     * to isolate whether PSRAM probe (pre-app_main) or a later peripheral
     * init is holding the pads at 0V. */
    buttons_init();

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
    /* If otadata points to a student app, boot it directly.
     * If otadata was erased (A+B held at power-on via bootloader hook),
     * preferred will be factory and we fall through to the loader. */
    try_launch_student_app();

    run_factory_loader();
}
