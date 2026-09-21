/*
 * UART TX/RX bring-up and verification for the JC8012P4A1.
 *
 * Why a second UART instead of UART0:
 *   UART0 (GPIO37 TX / GPIO38 RX) is the ESP-IDF console. It is wired to the
 *   on-board CH340 and to header CN2. Reconfiguring it for a loopback test
 *   while the console driver owns it produces meaningless results, so the
 *   tests here run on a separate port on free expansion-header pins.
 *
 * Two tests are provided:
 *   INTERNAL - uart_set_loop_back(): TX is looped to RX inside the UART
 *              peripheral. No wiring required. Proves the port, the driver,
 *              the clock and the baud divisor are correct, but says nothing
 *              about the pads or the board wiring.
 *   EXTERNAL - the real thing: bytes leave on the TX pad and come back in on
 *              the RX pad. Requires a jumper between the two test pins. This
 *              is the test that actually verifies TX/RX.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BSP_UART_TEST_INTERNAL = 0, /**< peripheral loopback, no wiring needed  */
    BSP_UART_TEST_EXTERNAL = 1, /**< needs a TX<->RX jumper on the header   */
} bsp_uart_test_mode_t;

typedef struct {
    bool      valid;          /**< false until a test has been run          */
    bool      pass;
    bsp_uart_test_mode_t mode;
    int       port;
    int       tx_gpio;
    int       rx_gpio;
    uint32_t  baud;
    size_t    bytes_sent;
    size_t    bytes_received;
    size_t    bytes_matched;
    int64_t   elapsed_us;
    esp_err_t err;
    char      detail[96];
} bsp_uart_test_result_t;

/** Install the UART driver on the test port. Safe to call more than once. */
esp_err_t bsp_uart_test_init(void);

/** Change the baud rate of the test port (also used by echo mode). */
esp_err_t bsp_uart_test_set_baud(uint32_t baud);
uint32_t  bsp_uart_test_get_baud(void);

/**
 * Run one loopback test. Sends a known pattern, reads it back and compares.
 * Blocks for up to ~500 ms. Never returns ESP_OK with pass == false unless the
 * data itself mismatched; transport failures come back in result->err.
 */
esp_err_t bsp_uart_test_run(bsp_uart_test_mode_t mode, bsp_uart_test_result_t *out);

/** Result of the most recent bsp_uart_test_run(), for the UI. */
void bsp_uart_test_get_last(bsp_uart_test_result_t *out);

/** Echo mode: everything arriving on RX is written straight back to TX. */
esp_err_t bsp_uart_echo_start(void);
esp_err_t bsp_uart_echo_stop(void);
bool      bsp_uart_echo_is_running(void);

/** Send an arbitrary string out of the test port. */
esp_err_t bsp_uart_test_send(const char *str);

/** Running byte counters and error counts since boot. */
void bsp_uart_test_stats(uint32_t *out_tx_bytes, uint32_t *out_rx_bytes,
                         uint32_t *out_errors);

/** Log a one-line PASS/FAIL summary through ESP_LOG. */
void bsp_uart_test_log(const bsp_uart_test_result_t *res);

#ifdef __cplusplus
}
#endif
