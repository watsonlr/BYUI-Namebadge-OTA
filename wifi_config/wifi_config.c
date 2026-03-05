/**
 * @file wifi_config.c
 * @brief SoftAP + HTTP captive-config portal for the BYUI eBadge.
 *
 * Flow:
 *   1. wifi_config_start() → starts AP "BADGE-CONFIG" (open, ch 1)
 *   2. HTTP server on 192.168.4.1:80  GET / → HTML form
 *                                     POST /save → save SSID+pass to NVS
 *   3. Caller polls wifi_config_done() or waits; then calls wifi_config_stop()
 */

#include "wifi_config.h"

#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define TAG "wifi_config"

#define AP_SSID      "BADGE-CONFIG"
#define AP_CHANNEL   1
#define AP_MAX_CONN  4

static httpd_handle_t  s_server   = NULL;
static bool            s_done     = false;
static esp_netif_t    *s_ap_netif = NULL;

/* ── HTML page ──────────────────────────────────────────────────── */
static const char *HTML_FORM =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Badge Config</title>"
    "<style>body{font-family:sans-serif;max-width:360px;margin:40px auto;padding:0 16px}"
    "input{width:100%;box-sizing:border-box;padding:8px;margin:6px 0 16px}"
    "button{width:100%;padding:10px;background:#006eb8;color:#fff;border:none;border-radius:4px;font-size:1em}"
    "</style></head><body>"
    "<h2>BYUI Badge Wi-Fi Setup</h2>"
    "<form method='POST' action='/save'>"
    "<label>Wi-Fi SSID<br><input name='ssid' required maxlength='32'></label>"
    "<label>Password (leave blank for open)<br><input name='pass' type='password' maxlength='64'></label>"
    "<button type='submit'>Save &amp; Connect</button>"
    "</form></body></html>";

static const char *HTML_OK =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Saved</title></head><body>"
    "<h2>Saved!</h2><p>The badge will now connect to your network. "
    "You may close this page.</p></body></html>";

/* ── Helpers ─────────────────────────────────────────────────────── */

/** Percent-decode a URL-encoded string in-place. */
static void url_decode(char *buf, size_t buflen)
{
    char *src = buf;
    char *dst = buf;
    char tmp[3] = {0};
    size_t written = 0;

    while (*src && written + 1 < buflen) {
        if (*src == '%' && src[1] && src[2]) {
            tmp[0] = src[1]; tmp[1] = src[2];
            *dst++ = (char)strtol(tmp, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
        written++;
    }
    *dst = '\0';
}

/** Extract a field value from an x-www-form-urlencoded body. */
static bool form_field(const char *body, const char *key,
                        char *out, size_t outlen)
{
    size_t klen = strlen(key);
    const char *p = body;
    while (*p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            p += klen + 1;
            size_t i = 0;
            while (*p && *p != '&' && i + 1 < outlen) {
                out[i++] = *p++;
            }
            out[i] = '\0';
            url_decode(out, outlen);
            return true;
        }
        /* Skip to next field */
        while (*p && *p != '&') p++;
        if (*p == '&') p++;
    }
    return false;
}

/* ── HTTP handlers ───────────────────────────────────────────────── */

static esp_err_t get_root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_FORM, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t post_save_handler(httpd_req_t *req)
{
    /* Limit body size to prevent overflow: SSID(32) + pass(64) + keys + & */
    char body[200] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char ssid[33] = {0};
    char pass[65] = {0};

    if (!form_field(body, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
        return ESP_FAIL;
    }
    form_field(body, "pass", pass, sizeof(pass)); /* optional */

    /* Write to NVS */
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        nvs_set_str(h, WIFI_CONFIG_NVS_KEY_SSID, ssid);
        nvs_set_str(h, WIFI_CONFIG_NVS_KEY_PASS, pass);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Saved SSID: %s", ssid);
        s_done = true;
    } else {
        ESP_LOGE(TAG, "NVS write failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS error");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_OK, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ── HTTP server ─────────────────────────────────────────────────── */

static const httpd_uri_t uri_root = {
    .uri     = "/",
    .method  = HTTP_GET,
    .handler = get_root_handler,
};

static const httpd_uri_t uri_save = {
    .uri     = "/save",
    .method  = HTTP_POST,
    .handler = post_save_handler,
};

static bool start_http_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = 80;
    cfg.max_open_sockets = 4;

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed");
        return false;
    }
    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_save);
    ESP_LOGI(TAG, "HTTP server started on port 80");
    return true;
}

/* ── SoftAP ──────────────────────────────────────────────────────── */

static bool start_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid            = AP_SSID,
            .ssid_len        = strlen(AP_SSID),
            .channel         = AP_CHANNEL,
            .authmode        = WIFI_AUTH_OPEN,
            .max_connection  = AP_MAX_CONN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP started: SSID=\"%s\"  IP=192.168.4.1", AP_SSID);
    return true;
}

/* ── Public API ──────────────────────────────────────────────────── */

bool wifi_config_start(void)
{
    nvs_flash_init(); /* harmless if already initialised */
    s_done = false;

    if (!start_softap())      return false;
    if (!start_http_server()) return false;
    return true;
}

void wifi_config_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }
}

bool wifi_config_done(void)
{
    return s_done;
}

const char *wifi_config_url(void)
{
    return "http://192.168.4.1/";
}
