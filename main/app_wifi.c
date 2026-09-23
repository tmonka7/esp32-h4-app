/*
 * Wi-Fi SoftAP through the ESP32-C6 co-processor (ESP-Hosted host mode).
 *
 * Nothing here is Hosted-specific: esp_wifi_remote provides esp_wifi_* on the
 * P4 and forwards each call to the C6, and esp_hosted's constructor has
 * already brought up the SDIO transport by the time app_main runs. The code
 * is the stock ESP-IDF SoftAP sequence.
 *
 * The AP netif also runs the DHCP server (esp_netif starts it with the
 * interface), so a joining client gets 192.168.4.2 upwards while the panel
 * keeps 192.168.4.1.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "app_wifi.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

static const char *TAG = "app_wifi";

/* WPA2-PSK passphrase limits. An 8-character minimum is a protocol rule, not
 * a policy choice: the radio rejects anything shorter. */
#define AP_PASSWORD_MIN 8

#ifdef CONFIG_APP_WIFI_HIDDEN
#define AP_SSID_HIDDEN 1
#else
#define AP_SSID_HIDDEN 0
#endif

typedef enum {
    WIFI_ST_OFF = 0,
    WIFI_ST_STARTING,
    WIFI_ST_UP,
} wifi_st_t;

static volatile wifi_st_t s_state;
static volatile unsigned  s_clients;
static esp_netif_t       *s_ap_netif;
static esp_ip4_addr_t     s_ip;

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    switch (id) {
    case WIFI_EVENT_AP_START: {
        esp_netif_ip_info_t info = { 0 };
        if (s_ap_netif != NULL && esp_netif_get_ip_info(s_ap_netif, &info) == ESP_OK) {
            s_ip = info.ip;
        }
        s_clients = 0;
        s_state = WIFI_ST_UP;
        ESP_LOGI(TAG, "AP \"%s\" up on channel %d, " IPSTR,
                 CONFIG_APP_WIFI_SSID, CONFIG_APP_WIFI_CHANNEL, IP2STR(&s_ip));
        break;
    }

    case WIFI_EVENT_AP_STOP:
        s_ip.addr = 0;
        s_clients = 0;
        s_state = WIFI_ST_STARTING;
        ESP_LOGW(TAG, "AP stopped");
        break;

    case WIFI_EVENT_AP_STACONNECTED: {
        const wifi_event_ap_staconnected_t *ev = data;
        s_clients++;
        if (ev != NULL) {
            ESP_LOGI(TAG, "client joined: " MACSTR " (aid %d), %u now associated",
                     MAC2STR(ev->mac), ev->aid, s_clients);
        }
        break;
    }

    case WIFI_EVENT_AP_STADISCONNECTED: {
        const wifi_event_ap_stadisconnected_t *ev = data;
        if (s_clients > 0) {
            s_clients--;
        }
        if (ev != NULL) {
            ESP_LOGI(TAG, "client left: " MACSTR " (aid %d), %u still associated",
                     MAC2STR(ev->mac), ev->aid, s_clients);
        }
        break;
    }

    default:
        break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    /* The panel is the DHCP server here, so the only address event that
     * concerns it is a lease handed to a client - useful for finding the
     * camera, which is otherwise silent about its address. */
    if (id == IP_EVENT_AP_STAIPASSIGNED) {
        const ip_event_ap_staipassigned_t *ev = data;
        if (ev != NULL) {
            ESP_LOGI(TAG, "leased " IPSTR " to a client", IP2STR(&ev->ip));
        }
    }
}

esp_err_t app_wifi_start(void)
{
    if (strlen(CONFIG_APP_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "no SSID configured (menuconfig > Wi-Fi); Wi-Fi left off");
        return ESP_ERR_INVALID_STATE;
    }

    const size_t pw_len = strlen(CONFIG_APP_WIFI_PASSWORD);
    if (pw_len > 0 && pw_len < AP_PASSWORD_MIN) {
        /* Starting an open AP instead would silently drop the protection the
         * configuration asked for, so refuse and say why. */
        ESP_LOGE(TAG, "password is %u characters; WPA2 needs at least %d "
                 "(or none at all for an open AP). Wi-Fi left off",
                 (unsigned)pw_len, AP_PASSWORD_MIN);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {   /* already created is fine */
        return err;
    }
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_ap_netif == NULL) {
        return ESP_FAIL;
    }

    const wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init: %s - is the C6 running ESP-Hosted slave firmware?",
                 esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_ip_event, NULL));

    wifi_config_t cfg = { 0 };
    const int ssid_len = snprintf((char *)cfg.ap.ssid, sizeof(cfg.ap.ssid), "%s",
                                  CONFIG_APP_WIFI_SSID);
    cfg.ap.ssid_len       = (uint8_t)(ssid_len < 0 ? 0 : ssid_len);
    cfg.ap.channel        = CONFIG_APP_WIFI_CHANNEL;
    cfg.ap.max_connection = CONFIG_APP_WIFI_MAX_CONN;
    cfg.ap.ssid_hidden    = AP_SSID_HIDDEN;
    snprintf((char *)cfg.ap.password, sizeof(cfg.ap.password), "%s", CONFIG_APP_WIFI_PASSWORD);
    /* An empty password means an open network; otherwise WPA2-PSK, which
     * every ESP32 client and every phone can join. PMF stays optional so
     * clients that do not implement it are not locked out. */
    cfg.ap.authmode         = pw_len ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.ap.pmf_cfg.capable  = true;
    cfg.ap.pmf_cfg.required = false;

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "starting the AP failed: %s", esp_err_to_name(err));
        return err;
    }

    s_state = WIFI_ST_STARTING;
    ESP_LOGI(TAG, "starting AP \"%s\" (%s) via the ESP32-C6",
             CONFIG_APP_WIFI_SSID, pw_len ? "WPA2" : "open");
    return ESP_OK;
}

bool app_wifi_is_connected(void)
{
    return s_state == WIFI_ST_UP;
}

unsigned app_wifi_client_count(void)
{
    return s_clients;
}

void app_wifi_status(char *out, size_t out_len)
{
    switch (s_state) {
    case WIFI_ST_OFF:
        snprintf(out, out_len, strlen(CONFIG_APP_WIFI_SSID) == 0
                 ? "Wi-Fi off: no SSID configured"
                 : "Wi-Fi off: AP bring-up failed, see console");
        break;
    case WIFI_ST_STARTING:
        snprintf(out, out_len, "Starting AP \"%s\"...", CONFIG_APP_WIFI_SSID);
        break;
    case WIFI_ST_UP: {
        esp_ip4_addr_t ip = s_ip;
        const unsigned n = s_clients;
        snprintf(out, out_len, "AP \"%s\" up, " IPSTR ", %u client%s",
                 CONFIG_APP_WIFI_SSID, IP2STR(&ip), n, n == 1 ? "" : "s");
        break;
    }
    }
}
