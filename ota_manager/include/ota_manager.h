/**
 * @file ota_manager.h
 * @brief PSRAM-buffered OTA update manager.
 *
 * Downloads the firmware binary into PSRAM, verifies its SHA-256 hash,
 * then writes the image atomically to the inactive OTA flash partition
 * and reboots.  Because the full image is validated in RAM before any
 * flash write begins, a power-loss during flashing leaves the previous
 * firmware intact.
 *
 * Expected call sequence (after portal_mode_run has saved credentials):
 *
 *   ota_result_t r = ota_manager_run();
 *   // OTA_RESULT_UPDATED never returns — device reboots automatically.
 *   // Any other result: handle error / proceed to badge display.
 *
 * Manifest JSON format (served from the URL saved in NVS "mfst"):
 *   {
 *     "version": 3,
 *     "url":     "https://host/badge_v3.bin",
 *     "size":    987654,
 *     "sha256":  "64 lowercase hex chars"
 *   }
 *
 * NVS namespace: "wifi_cfg"  (shared with wifi_config component)
 * NVS keys read: "ssid", "pass", "mfst"
 * NVS key written on success: "ota_ver"  (uint32, manifest version)
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_RESULT_UP_TO_DATE   = 0, /**< Manifest version <= installed version. */
    OTA_RESULT_UPDATED,          /**< Image flashed; device is rebooting.    */
    OTA_RESULT_NO_WIFI,          /**< No credentials or failed to connect.   */
    OTA_RESULT_NO_MANIFEST,      /**< Manifest URL missing or fetch failed.  */
    OTA_RESULT_DOWNLOAD_FAIL,    /**< Firmware binary download failed.       */
    OTA_RESULT_VERIFY_FAIL,      /**< SHA-256 mismatch.                      */
    OTA_RESULT_FLASH_FAIL,       /**< OTA partition write failed.            */
    OTA_RESULT_NO_PSRAM,         /**< PSRAM unavailable or too small.        */
} ota_result_t;

/**
 * @brief Run the OTA update check.
 *
 * Blocks until either an update is installed (then reboots) or the
 * check completes without finding a newer version.
 *
 * Prerequisite: esp_netif_init() and esp_event_loop_create_default()
 * must already have been called (portal_mode satisfies this).
 *
 * @return ota_result_t  Only non-UPDATED values return to the caller.
 */
ota_result_t ota_manager_run(void);

#ifdef __cplusplus
}
#endif
