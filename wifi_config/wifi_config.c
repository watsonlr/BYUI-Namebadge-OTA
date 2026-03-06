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
#include <stdint.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#define TAG "wifi_config"

#define AP_SSID      "BYUI_NameBadge"
#define AP_CHANNEL   1
#define AP_MAX_CONN  4

static httpd_handle_t  s_server      = NULL;
static bool            s_done        = false;
static esp_netif_t    *s_ap_netif    = NULL;
static TaskHandle_t    s_dns_task    = NULL;
static volatile bool   s_dns_running = false;

/* ── Captive-portal DNS hijack ──────────────────────────────────── */
/*
 * Listens on UDP port 53 and responds to every DNS query with an
 * A record pointing at 192.168.4.1.  This causes the phone OS to
 * detect a captive portal and automatically pop up the browser.
 */
#define DNS_BUF_SIZE 512

static void dns_server_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket create failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in srv = {
        .sin_family      = AF_INET,
        .sin_port        = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        ESP_LOGE(TAG, "DNS bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    /* 1-second receive timeout so we can check s_dns_running. */
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "DNS hijack task started");

    static uint8_t buf[DNS_BUF_SIZE];
    struct sockaddr_in client;
    socklen_t clen = sizeof(client);

    while (s_dns_running) {
        int n = recvfrom(sock, buf, sizeof(buf) - 20, 0,
                         (struct sockaddr *)&client, &clen);
        if (n < 12) continue;  /* timeout (-1) or truncated — skip */

        /* Turn the query into a response in-place:
         *   QR=1 (response), AA=1 (authoritative), RD copied, RA=1
         *   RCODE = 0 (no error), ANCOUNT = 1                      */
        buf[2] = (uint8_t)((buf[2] & 0x01) | 0x84); /* QR AA RD(kept) */
        buf[3] = 0x80;  /* RA=1, RCODE=0 */
        buf[6] = 0x00; buf[7] = 0x01;  /* ANCOUNT = 1 */
        buf[8] = 0x00; buf[9] = 0x00;  /* NSCOUNT = 0 */
        buf[10]= 0x00; buf[11]= 0x00;  /* ARCOUNT = 0 */

        /* Append answer RR: name ptr → offset 12, type A, class IN,
         * TTL 0 (don't cache), RDLEN 4, 192.168.4.1               */
        const uint8_t ans[] = {
            0xC0, 0x0C,              /* Name: pointer to offset 12  */
            0x00, 0x01,              /* Type: A                     */
            0x00, 0x01,              /* Class: IN                   */
            0x00, 0x00, 0x00, 0x00, /* TTL: 0                      */
            0x00, 0x04,              /* RDLENGTH: 4                 */
            192, 168, 4, 1           /* RDATA: 192.168.4.1          */
        };
        memcpy(buf + n, ans, sizeof(ans));
        sendto(sock, buf, n + sizeof(ans), 0,
               (struct sockaddr *)&client, clen);
    }

    close(sock);
    ESP_LOGI(TAG, "DNS hijack task stopped");
    vTaskDelete(NULL);
}

static void start_dns_server(void)
{
    s_dns_running = true;
    xTaskCreate(dns_server_task, "dns_hijack", 4096, NULL, 5, &s_dns_task);
}

static void stop_dns_server(void)
{
    s_dns_running = false;
    /* Task self-deletes after its next 1 s recv timeout. */
    s_dns_task = NULL;
}

/* ── HTML page ──────────────────────────────────────────────────── */
static const char *HTML_FORM =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>BYUI Badge Setup</title>"
    "<style>"
    "body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:0 16px}"
    "h2{color:#006eb8}"
    "label{display:block;font-weight:bold;font-size:.9em;margin-top:12px}"
    "input{width:100%;box-sizing:border-box;padding:8px;margin:4px 0 2px;"
          "border:1px solid #ccc;border-radius:4px}"
    "hr{border:none;border-top:1px solid #e0e0e0;margin:18px 0}"
    "button{margin-top:18px;width:100%;padding:12px;background:#006eb8;"
           "color:#fff;border:none;border-radius:4px;font-size:1em;cursor:pointer}"
    "</style></head><body>"
    "<h2>BYUI eBadge Setup</h2>"
    "<form method='POST' action='/save'>"
    "<label>Badge Nickname</label>"
    "<input name='nick' placeholder='e.g. Jane Smith' maxlength='32'>"
    "<hr>"
    "<label>Wi-Fi SSID <small>(for OTA updates)</small></label>"
    "<input name='ssid' required maxlength='32'>"
    "<label>Wi-Fi Password <small>(leave blank if open)</small></label>"
    "<input name='pass' type='password' maxlength='64'>"
    "<hr>"
    "<label>App Manifest URL</label>"
    "<input name='manifest' placeholder='https://yoursite.github.io/catalog.json' maxlength='128'>"
    "<button type='submit'>Save Settings</button>"
    "</form></body></html>";

static const char *HTML_OK =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Saved</title></head><body>"
    "<h2>Saved!</h2><p>The badge will now connect to your network. "
    "You may close this page.</p></body></html>";

/* ── Captive-portal redirect handler ────────────────────────────── */
/*
 * iOS, Android, Windows and macOS all probe a known URL after joining
 * an open network.  Because our DNS hijack returns 192.168.4.1 for any
 * hostname, those probes arrive here.  We 302-redirect them to our
 * setup page, which triggers the OS "Sign in to network" popup.
 */
static esp_err_t redirect_handler(httpd_req_t *req, httpd_err_code_t error)
{
    (void)error;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Redirecting to setup page", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

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
    /* Buffer: ssid(32)+pass(64)+nick(32)+manifest(128)+keys/=& ≈ 290 → 512 */
    char body[512] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char ssid[33]     = {0};
    char pass[65]     = {0};
    char nick[33]     = {0};
    char manifest[129] = {0};

    if (!form_field(body, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
        return ESP_FAIL;
    }
    form_field(body, "pass",     pass,     sizeof(pass));     /* optional */
    form_field(body, "nick",     nick,     sizeof(nick));     /* optional */
    form_field(body, "manifest", manifest, sizeof(manifest)); /* optional */

    /* Write to NVS */
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        nvs_set_str(h, WIFI_CONFIG_NVS_KEY_SSID,     ssid);
        nvs_set_str(h, WIFI_CONFIG_NVS_KEY_PASS,     pass);
        nvs_set_str(h, WIFI_CONFIG_NVS_KEY_NICK,     nick);
        nvs_set_str(h, WIFI_CONFIG_NVS_KEY_MANIFEST, manifest);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Saved: nick='%s'  ssid='%s'  manifest='%s'",
                 nick, ssid, manifest);
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
    cfg.lru_purge_enable = true;

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed");
        return false;
    }
    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_save);
    /* Redirect everything else → triggers OS captive-portal popup */
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, redirect_handler);
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
    start_dns_server();
    return true;
}

void wifi_config_stop(void)
{
    stop_dns_server();
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
