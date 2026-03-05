/**
 * @file wifi_config.h
 * @brief SoftAP + HTTP captive-config portal.
 *
 * Starts a Wi-Fi access point and a tiny HTTP server so a phone can
 * browse to http://192.168.4.1/ and POST station credentials that are
 * saved to NVS.  Call wifi_config_start() once; it runs in the
 * background until wifi_config_stop() is called.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** NVS namespace and keys written by the portal. */
#define WIFI_CONFIG_NVS_NAMESPACE "wifi_cfg"
#define WIFI_CONFIG_NVS_KEY_SSID  "ssid"
#define WIFI_CONFIG_NVS_KEY_PASS  "pass"

/**
 * @brief Start the SoftAP and HTTP configuration portal.
 *
 * Initialises NVS (if not already done), brings up the AP, starts the
 * HTTP server, and returns immediately.  The portal runs in the
 * background via ESP-IDF tasks.
 *
 * @return true on success, false if the AP or HTTP server failed to start.
 */
bool wifi_config_start(void);

/**
 * @brief Stop the portal and tear down the AP + HTTP server.
 */
void wifi_config_stop(void);

/**
 * @brief Check whether the user has submitted credentials via the portal.
 *
 * Credentials are written to NVS before this returns true.
 *
 * @return true once a valid SSID has been saved.
 */
bool wifi_config_done(void);

/**
 * @brief Return the URL a phone should open to reach the portal.
 *        Always "http://192.168.4.1/" — useful for QR code generation.
 */
const char *wifi_config_url(void);

#ifdef __cplusplus
}
#endif
