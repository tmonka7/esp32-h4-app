/*
 * Wi-Fi station through the ESP32-C6 co-processor (ESP-Hosted host mode).
 *
 * Nothing here is Hosted-specific: esp_wifi_remote provides esp_wifi_* on the
 * P4 and forwards each call to the C6, and esp_hosted's constructor has
 * already brought up the SDIO transport by the time app_main runs. The code
 * is the stock ESP-IDF station sequence.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "app_wifi.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

static const char *TAG = "app_wifi";

typedef enum {
    WIFI_ST_OFF = 0,
    WIFI_ST_CONNECTING,
    WIFI_ST_CONNECTED,
    WIFI_ST_DISCONNECTED,
} wifi_st_t;

static volatile wifi_st_t s_state;
static volatile int       s_last_reason;
static esp_ip4_addr_t     s_ip;

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    switch (id) {
    case WIFI_EVENT_STA_START:
        s_state = WIFI_ST_CONNECTING;
        esp_wifi_connect();
        break;

    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *ev = data;
        s_last_reason = ev != NULL ? ev->reason : 0;
        s_state = WIFI_ST_DISCONNECTED;
        s_ip.addr = 0;
        ESP_LOGW(TAG, "disconnected (reason %d), retrying", s_last_reason);
        /* A failed attempt already takes seconds (scan + auth timeout), so
         * retrying straight away does not spin. */
        esp_wifi_connect();
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

    if (id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = data;
        s_ip = ev->ip_info.ip;
        s_state = WIFI_ST_CONNECTED;
        ESP_LOGI(TAG, "connected to \"%s\", IP " IPSTR, CONFIG_APP_WIFI_SSID, IP2STR(&s_ip));
    } else if (id == IP_EVENT_STA_LOST_IP) {
        s_ip.addr = 0;
        s_state = WIFI_ST_CONNECTING;
    }
}

esp_err_t app_wifi_start(void)
{
    if (strlen(CONFIG_APP_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "no SSID configured (menuconfig > Wi-Fi); Wi-Fi left off");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {   /* already created is fine */
        return err;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
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
    snprintf((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid), "%s", CONFIG_APP_WIFI_SSID);
    snprintf((char *)cfg.sta.password, sizeof(cfg.sta.password), "%s", CONFIG_APP_WIFI_PASSWORD);
    /* An empty password means an open network; anything else accepts
     * WPA2 or better, so a WPA3 AP still works. */
    cfg.sta.threshold.authmode = strlen(CONFIG_APP_WIFI_PASSWORD) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "starting station failed: %s", esp_err_to_name(err));
        return err;
    }

    s_state = WIFI_ST_CONNECTING;
    ESP_LOGI(TAG, "joining \"%s\" via the ESP32-C6", CONFIG_APP_WIFI_SSID);
    return ESP_OK;
}

bool app_wifi_is_connected(void)
{
    return s_state == WIFI_ST_CONNECTED;
}

void app_wifi_status(char *out, size_t out_len)
{
    switch (s_state) {
    case WIFI_ST_OFF:
        snprintf(out, out_len, strlen(CONFIG_APP_WIFI_SSID) == 0
                 ? "Wi-Fi off: no SSID configured"
                 : "Wi-Fi off: bring-up failed, see console");
        break;
    case WIFI_ST_CONNECTING:
        snprintf(out, out_len, "Joining \"%s\"...", CONFIG_APP_WIFI_SSID);
        break;
    case WIFI_ST_CONNECTED: {
        esp_ip4_addr_t ip = s_ip;
        snprintf(out, out_len, "Connected to \"%s\", " IPSTR, CONFIG_APP_WIFI_SSID, IP2STR(&ip));
        break;
    }
    case WIFI_ST_DISCONNECTED:
        snprintf(out, out_len, "Lost \"%s\" (reason %d), retrying",
                 CONFIG_APP_WIFI_SSID, s_last_reason);
        break;
    }
}
