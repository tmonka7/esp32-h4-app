/*
 * UART TX/RX verification for the JC8012P4A1.
 *
 * See bsp/bsp_uart_test.h for why this runs on a separate port instead of
 * UART0 (GPIO37/38), which belongs to the console.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bsp/bsp_uart_test.h"
#include "bsp/jc8012p4a1.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp_uart";

#define TEST_PORT        ((uart_port_t)CONFIG_BSP_UART_TEST_PORT)
#define TEST_TX_GPIO     (CONFIG_BSP_UART_TEST_TX_GPIO)
#define TEST_RX_GPIO     (CONFIG_BSP_UART_TEST_RX_GPIO)
#define RX_BUF_BYTES     (1024)
#define TX_BUF_BYTES     (1024)

/* 64 bytes covering every bit pattern that tends to expose a wrong baud rate,
 * a stuck line or a swapped pair: alternating bits, all-ones, all-zeros, and
 * an incrementing ramp. */
static const uint8_t k_pattern[] = {
    0x55, 0xAA, 0x55, 0xAA, 0x00, 0xFF, 0x00, 0xFF,
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
    0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F,
    'J',  'C',  '8',  '0',  '1',  '2',  'P',  '4',
    'A',  '1',  '-',  'U',  'A',  'R',  'T',  '-',
    'L',  'O',  'O',  'P',  'B',  'A',  'C',  'K',
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
};

static bool     s_installed;
static uint32_t s_baud = CONFIG_BSP_UART_TEST_BAUD;
static uint32_t s_tx_bytes;
static uint32_t s_rx_bytes;
static uint32_t s_errors;

static TaskHandle_t s_echo_task;
static volatile bool s_echo_run;

static bsp_uart_test_result_t s_last;

/* ------------------------------------------------------------------ */

esp_err_t bsp_uart_test_init(void)
{
    if (s_installed) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(TEST_PORT != UART_NUM_0, ESP_ERR_INVALID_ARG, TAG,
                        "UART0 is the console; pick another port");

    const uart_config_t cfg = {
        .baud_rate  = (int)s_baud,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(TEST_PORT, RX_BUF_BYTES, TX_BUF_BYTES, 0, NULL, 0),
                        TAG, "uart_driver_install");
    ESP_RETURN_ON_ERROR(uart_param_config(TEST_PORT, &cfg), TAG, "uart_param_config");
    ESP_RETURN_ON_ERROR(uart_set_pin(TEST_PORT, TEST_TX_GPIO, TEST_RX_GPIO,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                        TAG, "uart_set_pin");

    s_installed = true;
    ESP_LOGI(TAG, "UART%d ready: TX=GPIO%d RX=GPIO%d @ %" PRIu32 " baud 8N1",
             (int)TEST_PORT, TEST_TX_GPIO, TEST_RX_GPIO, s_baud);
    return ESP_OK;
}

esp_err_t bsp_uart_test_set_baud(uint32_t baud)
{
    ESP_RETURN_ON_FALSE(baud >= 300 && baud <= 5000000, ESP_ERR_INVALID_ARG, TAG,
                        "baud %" PRIu32 " out of range", baud);
    if (s_installed) {
        ESP_RETURN_ON_ERROR(uart_set_baudrate(TEST_PORT, baud), TAG, "uart_set_baudrate");
    }
    s_baud = baud;
    return ESP_OK;
}

uint32_t bsp_uart_test_get_baud(void)
{
    return s_baud;
}

/* ------------------------------------------------------------------ */

esp_err_t bsp_uart_test_run(bsp_uart_test_mode_t mode, bsp_uart_test_result_t *out)
{
    bsp_uart_test_result_t res = {
        .valid    = true,
        .pass     = false,
        .mode     = mode,
        .port     = (int)TEST_PORT,
        .tx_gpio  = TEST_TX_GPIO,
        .rx_gpio  = TEST_RX_GPIO,
        .baud     = s_baud,
        .err      = ESP_OK,
    };
    const bool internal = (mode == BSP_UART_TEST_INTERNAL);

    esp_err_t ret = bsp_uart_test_init();
    if (ret != ESP_OK) {
        res.err = ret;
        snprintf(res.detail, sizeof(res.detail), "driver install failed: %s", esp_err_to_name(ret));
        goto done;
    }

    /* Echo mode would eat the reply; make the conflict explicit rather than
     * reporting a mysterious failure. */
    if (s_echo_run) {
        res.err = ESP_ERR_INVALID_STATE;
        snprintf(res.detail, sizeof(res.detail), "stop echo mode before running a loopback test");
        goto done;
    }

    if (internal) {
        ret = uart_set_loop_back(TEST_PORT, true);
        if (ret != ESP_OK) {
            res.err = ret;
            snprintf(res.detail, sizeof(res.detail), "uart_set_loop_back: %s", esp_err_to_name(ret));
            goto done;
        }
    }

    /* Drop anything already queued so a previous test or stray noise cannot
     * be mistaken for our echo. */
    uart_flush_input(TEST_PORT);

    const int64_t t0 = esp_timer_get_time();

    const int written = uart_write_bytes(TEST_PORT, k_pattern, sizeof(k_pattern));
    if (written < 0) {
        res.err = ESP_FAIL;
        snprintf(res.detail, sizeof(res.detail), "uart_write_bytes returned %d", written);
        goto restore;
    }
    res.bytes_sent = (size_t)written;
    s_tx_bytes += (uint32_t)written;

    /* Time for the bytes to go out and come back, plus slack. At 115200 baud
     * 64 bytes is ~5.6 ms; 500 ms is a generous ceiling that still keeps the
     * UI responsive. */
    uint8_t rx[sizeof(k_pattern)];
    memset(rx, 0, sizeof(rx));
    const int read = uart_read_bytes(TEST_PORT, rx, sizeof(k_pattern), pdMS_TO_TICKS(500));

    res.elapsed_us = esp_timer_get_time() - t0;

    if (read < 0) {
        res.err = ESP_FAIL;
        snprintf(res.detail, sizeof(res.detail), "uart_read_bytes returned %d", read);
        goto restore;
    }
    res.bytes_received = (size_t)read;
    s_rx_bytes += (uint32_t)read;

    size_t matched = 0;
    for (int i = 0; i < read; i++) {
        if (rx[i] == k_pattern[i]) {
            matched++;
        }
    }
    res.bytes_matched = matched;

    if (read == 0) {
        s_errors++;
        if (internal) {
            snprintf(res.detail, sizeof(res.detail),
                     "nothing received - UART%d peripheral is not looping back", (int)TEST_PORT);
        } else {
            snprintf(res.detail, sizeof(res.detail),
                     "nothing received - link GPIO%d (TX) to GPIO%d (RX)",
                     TEST_TX_GPIO, TEST_RX_GPIO);
        }
    } else if (read != (int)sizeof(k_pattern)) {
        s_errors++;
        snprintf(res.detail, sizeof(res.detail), "short read: %d of %d bytes",
                 read, (int)sizeof(k_pattern));
    } else if (matched != sizeof(k_pattern)) {
        s_errors++;
        snprintf(res.detail, sizeof(res.detail),
                 "%d of %d bytes corrupted - check baud rate and wiring",
                 (int)(sizeof(k_pattern) - matched), (int)sizeof(k_pattern));
    } else {
        res.pass = true;
        snprintf(res.detail, sizeof(res.detail), "%d/%d bytes returned intact in %" PRId64 " us",
                 read, (int)sizeof(k_pattern), res.elapsed_us);
    }

restore:
    if (internal) {
        /* Always drop peripheral loopback again, otherwise echo mode and any
         * real traffic afterwards would silently talk to themselves. */
        esp_err_t r2 = uart_set_loop_back(TEST_PORT, false);
        if (r2 != ESP_OK && res.err == ESP_OK) {
            res.err = r2;
        }
    }

done:
    s_last = res;
    if (out != NULL) {
        *out = res;
    }
    return res.err;
}

void bsp_uart_test_get_last(bsp_uart_test_result_t *out)
{
    if (out != NULL) {
        *out = s_last;
    }
}

void bsp_uart_test_log(const bsp_uart_test_result_t *res)
{
    if (res == NULL || !res->valid) {
        ESP_LOGW(TAG, "no UART test has been run yet");
        return;
    }

    const char *mode = (res->mode == BSP_UART_TEST_INTERNAL) ? "internal" : "external";

    if (res->pass) {
        ESP_LOGI(TAG, "UART%d %s loopback PASS - TX=GPIO%d RX=GPIO%d @ %" PRIu32 " baud, %s",
                 res->port, mode, res->tx_gpio, res->rx_gpio, res->baud, res->detail);
    } else {
        ESP_LOGE(TAG, "UART%d %s loopback FAIL - TX=GPIO%d RX=GPIO%d @ %" PRIu32 " baud, %s",
                 res->port, mode, res->tx_gpio, res->rx_gpio, res->baud, res->detail);
    }
}

/* ------------------------------------------------------------------ */

static void echo_task(void *arg)
{
    (void)arg;
    uint8_t buf[128];

    ESP_LOGI(TAG, "echo mode on: anything arriving on GPIO%d is sent back out GPIO%d",
             TEST_RX_GPIO, TEST_TX_GPIO);

    while (s_echo_run) {
        const int n = uart_read_bytes(TEST_PORT, buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (n > 0) {
            s_rx_bytes += (uint32_t)n;
            const int w = uart_write_bytes(TEST_PORT, buf, (size_t)n);
            if (w > 0) {
                s_tx_bytes += (uint32_t)w;
            }
            if (w != n) {
                s_errors++;
            }
        } else if (n < 0) {
            s_errors++;
        }
    }

    ESP_LOGI(TAG, "echo mode off");
    s_echo_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t bsp_uart_echo_start(void)
{
    if (s_echo_run) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(bsp_uart_test_init(), TAG, "uart init");

    s_echo_run = true;
    if (xTaskCreate(echo_task, "uart_echo", 3072, NULL, 5, &s_echo_task) != pdPASS) {
        s_echo_run = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t bsp_uart_echo_stop(void)
{
    if (!s_echo_run) {
        return ESP_OK;
    }
    s_echo_run = false;
    /* The task wakes at most 100 ms later, deletes itself and clears the
     * handle. Wait for that so a restart cannot race the old task. */
    for (int i = 0; i < 20 && s_echo_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

bool bsp_uart_echo_is_running(void)
{
    return s_echo_run;
}

esp_err_t bsp_uart_test_send(const char *str)
{
    ESP_RETURN_ON_FALSE(str != NULL, ESP_ERR_INVALID_ARG, TAG, "null string");
    ESP_RETURN_ON_ERROR(bsp_uart_test_init(), TAG, "uart init");

    const int n = uart_write_bytes(TEST_PORT, str, strlen(str));
    if (n < 0) {
        s_errors++;
        return ESP_FAIL;
    }
    s_tx_bytes += (uint32_t)n;
    return ESP_OK;
}

void bsp_uart_test_stats(uint32_t *out_tx_bytes, uint32_t *out_rx_bytes, uint32_t *out_errors)
{
    if (out_tx_bytes) {
        *out_tx_bytes = s_tx_bytes;
    }
    if (out_rx_bytes) {
        *out_rx_bytes = s_rx_bytes;
    }
    if (out_errors) {
        *out_errors = s_errors;
    }
}
