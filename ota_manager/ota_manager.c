/**
 * @file ota_manager.c
 * @brief PSRAM-buffered OTA update manager.
 *
 * Flow:
 *   1. Read WiFi credentials + manifest URL from NVS.
 *   2. Connect to the saved AP (STA mode).
 *   3. Fetch the JSON manifest → parse version / URL / size / sha256.
 *   4. Compare manifest version against last installed version (NVS).
 *   5. Allocate the full firmware image in PSRAM.
 *   6. HTTP-stream the binary directly into the PSRAM buffer.
 *   7. Disconnect WiFi (flash SPI is independent of the radio).
 *   8. Verify SHA-256 over the PSRAM buffer.
 *   9. Write the image to the inactive OTA partition in one call.
 *  10. Mark the partition bootable, store new version, reboot.
 */

#include "ota_manager.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "mbedtls/sha256.h"
#include "cJSON.h"

#include "display.h"

#define TAG "ota_manager"

/* ── NVS ──────────────────────────────────────────────────────────── */
/* Read/write the same dedicated user_data partition used by wifi_config. */
#define NVS_PARTITION  "user_data"
#define NVS_NAMESPACE  "badge_cfg"
#define NVS_KEY_SSID   "ssid"
#define NVS_KEY_PASS   "pass"
#define NVS_KEY_MFST   "mfst"
#define NVS_KEY_OTAVER "ota_ver"   /* uint32 — last installed manifest ver */

/* ── WiFi STA ─────────────────────────────────────────────────────── */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_MAX_RETRIES    5
#define WIFI_TIMEOUT_MS     30000

static EventGroupHandle_t s_wifi_eg   = NULL;
static esp_netif_t       *s_sta_netif = NULL;
static int                s_retries   = 0;

/* ── display helper ───────────────────────────────────────────────── */
static void show_status(const char *line1, const char *line2)
{
    display_fill(DISPLAY_COLOR_BLACK);
    display_text_ctx_t ctx = DISPLAY_CTX(DISPLAY_FONT_SANS, 1,
                                          DISPLAY_COLOR_WHITE,
                                          DISPLAY_COLOR_BLACK);
    if (line1) display_print(&ctx,  8, 108, line1);
    if (line2) display_print(&ctx,  8, 124, line2);
}

/* ── WiFi event handler ───────────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retries < WIFI_MAX_RETRIES) {
            s_retries++;
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_eg, WIFI_FAIL_BIT);
        }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retries = 0;
        xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
    }
}

/* ── WiFi STA connect / disconnect ───────────────────────────────── */
static bool wifi_sta_connect(const char *ssid, const char *pass)
{
    s_wifi_eg = xEventGroupCreate();
    s_retries = 0;

    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    esp_event_handler_instance_t inst_wifi, inst_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, &inst_wifi));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, &inst_ip));

    wifi_config_t sta_cfg = {0};
    strlcpy((char *)sta_cfg.sta.ssid,     ssid, sizeof(sta_cfg.sta.ssid));
    strlcpy((char *)sta_cfg.sta.password, pass, sizeof(sta_cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_eg,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_TIMEOUT_MS));

    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, inst_ip);
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, inst_wifi);
    vEventGroupDelete(s_wifi_eg);
    s_wifi_eg = NULL;

    return (bits & WIFI_CONNECTED_BIT) != 0;
}

static void wifi_sta_disconnect(void)
{
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_sta_netif) {
        esp_netif_destroy(s_sta_netif);
        s_sta_netif = NULL;
    }
}

/* ── HTTP: fetch small text resource (manifest JSON) ─────────────── */
static esp_err_t http_get_text(const char *url, char *out, size_t out_max,
                                int *out_len)
{
    esp_http_client_config_t cfg = {
        .url               = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 10000,
        .max_redirection_count = 3,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    esp_http_client_fetch_headers(client);

    if (esp_http_client_get_status_code(client) != 200) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int total = 0;
    while (total < (int)out_max - 1) {
        int n = esp_http_client_read(client, out + total,
                                     (int)out_max - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    out[total] = '\0';
    *out_len = total;

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return total > 0 ? ESP_OK : ESP_FAIL;
}

/* ── Stream firmware from HTTP directly into OTA flash partition ──── *
 *
 * Downloads in 8 KB chunks, hashes incrementally with SHA-256, and
 * writes each chunk to the inactive OTA partition as it arrives.
 * No PSRAM or large RAM buffer required.                              */
#define OTA_CHUNK_SIZE 8192
static uint8_t s_ota_chunk[OTA_CHUNK_SIZE];   /* static → BSS, not stack */

static esp_err_t http_stream_and_flash(const char *url, int expected_size,
                                        const char *sha256_expected)
{
    /* Open OTA partition for writing */
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        ESP_LOGE(TAG, "No OTA partition available");
        return ESP_FAIL;
    }
    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(part, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        return err;
    }

    esp_http_client_config_t cfg = {
        .url               = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 120000,
        .buffer_size       = OTA_CHUNK_SIZE,
        .max_redirection_count = 3,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { esp_ota_abort(ota_handle); return ESP_FAIL; }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        esp_ota_abort(ota_handle);
        return err;
    }
    esp_http_client_fetch_headers(client);
    if (esp_http_client_get_status_code(client) != 200) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        esp_ota_abort(ota_handle);
        return ESP_FAIL;
    }

    /* SHA-256 context — hashed incrementally as chunks arrive */
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);

    int total = 0, last_pct = -1;
    err = ESP_OK;

    while (total < expected_size) {
        int want = expected_size - total;
        if (want > OTA_CHUNK_SIZE) want = OTA_CHUNK_SIZE;
        int n = esp_http_client_read(client, (char *)s_ota_chunk, want);
        if (n < 0) { err = ESP_FAIL; break; }
        if (n == 0)  break;

        mbedtls_sha256_update(&sha_ctx, s_ota_chunk, n);

        esp_err_t we = esp_ota_write(ota_handle, s_ota_chunk, n);
        if (we != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(we));
            err = we;
            break;
        }
        total += n;

        int pct = (int)(100LL * total / expected_size);
        if (pct / 5 != last_pct / 5) {
            last_pct = pct;
            char prog[32];
            snprintf(prog, sizeof(prog), "%d%%  (%d KB)", pct, total / 1024);
            show_status("Downloading...", prog);
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || total != expected_size) {
        ESP_LOGE(TAG, "Download incomplete: %d of %d bytes", total, expected_size);
        mbedtls_sha256_free(&sha_ctx);
        esp_ota_abort(ota_handle);
        show_status("Download failed", NULL);
        return ESP_FAIL;
    }

    /* Verify SHA-256 over the received data */
    uint8_t hash[32];
    mbedtls_sha256_finish(&sha_ctx, hash);
    mbedtls_sha256_free(&sha_ctx);
    char computed[65] = {0};
    for (int i = 0; i < 32; i++) snprintf(computed + 2*i, 3, "%02x", hash[i]);
    if (strcmp(computed, sha256_expected) != 0) {
        ESP_LOGE(TAG, "SHA-256 mismatch\n  expected: %s\n  computed: %s",
                 sha256_expected, computed);
        esp_ota_abort(ota_handle);
        show_status("SHA-256 mismatch!", "Update aborted");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "SHA-256 verified OK");

    /* Finalise and mark partition bootable */
    show_status("Installing update...", "Do not power off");
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
        show_status("Flash write failed", NULL);
        return err;
    }
    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
    }
    return err;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════════════════ */
ota_result_t ota_manager_run(void)
{
    /* ── Read NVS credentials ─────────────────────────────────── */
    /* Ensure the user_data partition is mounted (idempotent). */
    esp_err_t ue = nvs_flash_init_partition(NVS_PARTITION);
    if (ue == ESP_ERR_NVS_NO_FREE_PAGES || ue == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase_partition(NVS_PARTITION);
        nvs_flash_init_partition(NVS_PARTITION);
    }

    char ssid[33]     = {0};
    char pass[65]     = {0};
    char mfst_url[129] = {0};

    nvs_handle_t h;
    if (nvs_open_from_partition(NVS_PARTITION, NVS_NAMESPACE,
                                NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGE(TAG, "user_data NVS open failed");
        return OTA_RESULT_NO_WIFI;
    }
    size_t n;
    n = sizeof(ssid);     nvs_get_str(h, NVS_KEY_SSID, ssid,     &n);
    n = sizeof(pass);     nvs_get_str(h, NVS_KEY_PASS, pass,     &n);
    n = sizeof(mfst_url); nvs_get_str(h, NVS_KEY_MFST, mfst_url, &n);
    nvs_close(h);

    if (ssid[0] == '\0') {
        ESP_LOGW(TAG, "No WiFi SSID in NVS — skipping OTA");
        return OTA_RESULT_NO_WIFI;
    }
    if (mfst_url[0] == '\0') {
        ESP_LOGW(TAG, "No manifest URL in NVS — skipping OTA");
        return OTA_RESULT_NO_MANIFEST;
    }

    /* ── Read last installed version ──────────────────────────── */
    uint32_t installed_ver = 0;
    nvs_handle_t hv;
    if (nvs_open_from_partition(NVS_PARTITION, NVS_NAMESPACE,
                                NVS_READONLY, &hv) == ESP_OK) {
        nvs_get_u32(hv, NVS_KEY_OTAVER, &installed_ver);
        nvs_close(hv);
    }

    /* ── Connect to WiFi ──────────────────────────────────────── */
    show_status("Connecting to WiFi...", ssid);
    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

    if (!wifi_sta_connect(ssid, pass)) {
        ESP_LOGE(TAG, "WiFi connect failed");
        show_status("WiFi connect failed", ssid);
        wifi_sta_disconnect();
        return OTA_RESULT_NO_WIFI;
    }
    ESP_LOGI(TAG, "WiFi connected");

    /* ── Fetch manifest ───────────────────────────────────────── */
    show_status("Checking for updates...", NULL);

    static char manifest_json[512];
    int manifest_len = 0;
    if (http_get_text(mfst_url, manifest_json, sizeof(manifest_json),
                      &manifest_len) != ESP_OK) {
        ESP_LOGE(TAG, "Manifest fetch failed: %s", mfst_url);
        show_status("Manifest fetch failed", NULL);
        wifi_sta_disconnect();
        return OTA_RESULT_NO_MANIFEST;
    }
    ESP_LOGI(TAG, "Manifest (%d B): %s", manifest_len, manifest_json);

    /* ── Parse manifest JSON ──────────────────────────────────── */
    cJSON *root = cJSON_Parse(manifest_json);
    if (!root) {
        ESP_LOGE(TAG, "Manifest JSON parse error");
        show_status("Manifest parse error", NULL);
        wifi_sta_disconnect();
        return OTA_RESULT_NO_MANIFEST;
    }

    cJSON *j_ver  = cJSON_GetObjectItem(root, "version");
    cJSON *j_url  = cJSON_GetObjectItem(root, "url");
    cJSON *j_size = cJSON_GetObjectItem(root, "size");
    cJSON *j_sha  = cJSON_GetObjectItem(root, "sha256");

    if (!cJSON_IsNumber(j_ver) || !cJSON_IsString(j_url) ||
        !cJSON_IsNumber(j_size) || !cJSON_IsString(j_sha)) {
        ESP_LOGE(TAG, "Manifest missing required fields");
        show_status("Manifest invalid", NULL);
        cJSON_Delete(root);
        wifi_sta_disconnect();
        return OTA_RESULT_NO_MANIFEST;
    }

    uint32_t manifest_ver = (uint32_t)j_ver->valuedouble;
    int      fw_size      = (int)j_size->valuedouble;

    /* Copy strings out before cJSON_Delete */
    char fw_url[257]    = {0};
    char sha256_hex[65] = {0};
    strlcpy(fw_url,    j_url->valuestring, sizeof(fw_url));
    strlcpy(sha256_hex, j_sha->valuestring, sizeof(sha256_hex));
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Manifest: v%"PRIu32", size=%d, url=%s",
             manifest_ver, fw_size, fw_url);

    /* ── Version check ────────────────────────────────────────── */
    if (manifest_ver <= installed_ver) {
        ESP_LOGI(TAG, "Up to date (installed v%"PRIu32", manifest v%"PRIu32")",
                 installed_ver, manifest_ver);
        show_status("Firmware up to date", NULL);
        vTaskDelay(pdMS_TO_TICKS(2000));
        wifi_sta_disconnect();
        return OTA_RESULT_UP_TO_DATE;
    }

    /* ── Verify image fits in the OTA partition ───────────────── */
    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        ESP_LOGE(TAG, "No OTA partition available");
        wifi_sta_disconnect();
        return OTA_RESULT_FLASH_FAIL;
    }
    if (fw_size <= 0 || (uint32_t)fw_size > update_part->size) {
        ESP_LOGE(TAG, "Firmware size %d exceeds partition size %"PRIu32,
                 fw_size, update_part->size);
        show_status("OTA: image too large", NULL);
        wifi_sta_disconnect();
        return OTA_RESULT_FLASH_FAIL;
    }

    /* ── Stream download → hash → flash (no PSRAM needed) ────── */
    show_status("Downloading...", NULL);
    esp_err_t ota_err = http_stream_and_flash(fw_url, fw_size, sha256_hex);
    wifi_sta_disconnect();
    if (ota_err != ESP_OK) {
        return OTA_RESULT_DOWNLOAD_FAIL;
    }

    /* ── Commit version to NVS ───────────────────────────────── */
    nvs_handle_t hv2;
    if (nvs_open_from_partition(NVS_PARTITION, NVS_NAMESPACE,
                                NVS_READWRITE, &hv2) == ESP_OK) {
        nvs_set_u32(hv2, NVS_KEY_OTAVER, manifest_ver);
        nvs_commit(hv2);
        nvs_close(hv2);
    }

    /* ── Reboot into new partition ───────────────────────────── */
    ESP_LOGI(TAG, "OTA complete — rebooting into v%"PRIu32, manifest_ver);
    show_status("Update complete!", "Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();

    return OTA_RESULT_UPDATED;  /* unreachable */
}
