/*
 * Wi-Fi SoftAP through the ESP32-C6 co-processor.
 *
 * The ESP32-P4 has no radio. It runs as the ESP-Hosted *host*: the C6 runs
 * the ESP-Hosted-MCU slave firmware and is reached over SDIO slot 1, and
 * esp_wifi_remote turns the ordinary esp_wifi_* calls into RPCs to it.
 *
 * The panel is the access point (WIFI_MODE_AP): it beacons
 * CONFIG_APP_WIFI_SSID, runs the DHCP server, and sits at 192.168.4.1.
 * Clients - the camera, a phone - join it. There is no station link and no
 * uplink to a router.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bring up netif + Wi-Fi and start the SoftAP CONFIG_APP_WIFI_SSID.
 *  Returns ESP_ERR_INVALID_STATE if no SSID is set (Wi-Fi stays off), or
 *  ESP_ERR_INVALID_ARG if the configured password is too short for WPA2. */
esp_err_t app_wifi_start(void);

/** True once the AP is running and the network is usable - which is not the
 *  same as anyone having joined it, see app_wifi_client_count(). */
bool app_wifi_is_connected(void);

/** Stations currently associated with the AP. */
unsigned app_wifi_client_count(void);

/** One-line human readable status, e.g. "AP \"panel\" up, 192.168.4.1, 1 client". */
void app_wifi_status(char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
