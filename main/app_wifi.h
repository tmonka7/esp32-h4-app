/*
 * Wi-Fi station through the ESP32-C6 co-processor.
 *
 * The ESP32-P4 has no radio. It runs as the ESP-Hosted *host*: the C6 runs
 * the ESP-Hosted-MCU slave firmware and is reached over SDIO slot 1, and
 * esp_wifi_remote turns the ordinary esp_wifi_* calls into RPCs to it.
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

/** Bring up netif + Wi-Fi and join CONFIG_APP_WIFI_SSID. Reconnects on its
 *  own after a drop. Returns ESP_ERR_INVALID_STATE if no SSID is set. */
esp_err_t app_wifi_start(void);

/** True once the station has an IPv4 address. */
bool app_wifi_is_connected(void);

/** One-line human readable status, e.g. "Connected to cam, 192.168.4.2". */
void app_wifi_status(char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
