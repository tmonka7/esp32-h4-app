# UART TX/RX verification

## Why not UART0

UART0 (GPIO37 TX / GPIO38 RX) is the ESP-IDF console. It leaves the module on
those two pins and is wired both to the on-board CH340 and to the 4-pin MX1.25
header **CN2** (1 = VIN, 2 = TXD, 3 = RXD, 4 = GND).

Two reasons the tests do not run on it:

1. The console driver owns the port. Reconfiguring it underneath, looping it
   back and then reading whatever the logger happens to have queued does not
   test anything meaningful.
2. It does not need testing. If you can read the boot log, UART0 TX works. If
   `idf.py monitor` accepts keystrokes, UART0 RX works too.

So the tests use a second port on free expansion-header pins.

## Configuration

`idf.py menuconfig` → *Board Support Package (JC8012P4A1)* → *UART test port*

| Option | Default | Meaning |
|---|---|---|
| `BSP_UART_TEST_PORT` | 1 | UART peripheral. Must not be 0. |
| `BSP_UART_TEST_TX_GPIO` | 45 | TX pin — free on FPC3 |
| `BSP_UART_TEST_RX_GPIO` | 46 | RX pin — free on FPC3 |
| `BSP_UART_TEST_BAUD` | 115200 | 8N1, no flow control |
| `BSP_UART_TEST_ON_BOOT` | y | run the internal test during startup |

FPC3 also exposes GPIO47, 48, 5, 4, 3 and 2; FPC4 exposes GPIO28–34. Any pair
works.

## The two tests

Both send the same 64-byte pattern and compare what comes back byte for byte.
The pattern is chosen to expose the failures that actually happen:

| Bytes | Catches |
|---|---|
| `55 AA 55 AA` | wrong baud rate, bad sampling point |
| `00 FF 00 FF` | stuck-high or stuck-low line |
| `01 02 04 … 80` | a single dead data bit |
| `FE FD FB … 7F` | the same, inverted |
| `"JC8012P4A1-UART-LOOPBACK"` | readable in a scope capture or a terminal |
| `10 11 … 1F` | framing drift over a longer run |

### Internal loopback

Uses `uart_set_loop_back(port, true)`, which ties TX to RX **inside the UART
peripheral**. No wiring needed.

Proves: the port exists, the driver installed, the clock source and baud
divisor are right, and the FIFO path works.

Does **not** prove: anything about the pads, the pin mux, or the board wiring —
the signal never leaves the SoC.

Runs automatically at boot when `BSP_UART_TEST_ON_BOOT` is set. The loopback
bit is always cleared afterwards, so echo mode and real traffic are not left
talking to themselves.

### External loopback

The real test. Put a jumper between the TX and RX pins on FPC3, then press
**External loopback** on the UART screen.

Proves: the pads drive and receive, the pin mux is correct, and the board
wiring out to the header is intact.

Without the jumper it will correctly report:

```
nothing received - link GPIO45 (TX) to GPIO46 (RX)
```

## Reading the result

On screen you get PASS/FAIL plus bytes sent / received / matched and a
one-line diagnosis. On the console:

```
I (1234) bsp_uart: UART1 ready: TX=GPIO45 RX=GPIO46 @ 115200 baud 8N1
I (1240) bsp_uart: UART1 internal loopback PASS - TX=GPIO45 RX=GPIO46 @ 115200 baud,
                   64/64 bytes returned intact in 812 us
```

Failure looks like:

```
E (5678) bsp_uart: UART1 external loopback FAIL - TX=GPIO45 RX=GPIO46 @ 115200 baud,
                   17 of 64 bytes corrupted - check baud rate and wiring
```

### Interpreting failures

| Symptom | Likely cause |
|---|---|
| Internal fails | wrong port number, driver not installed, or the port is already in use |
| Internal passes, external receives nothing | no jumper, wrong GPIO numbers, or the pin is shorted/damaged |
| External returns a short read | baud mismatch between config and the far end, or marginal signal integrity |
| External returns corrupted bytes | baud mismatch, missing common ground, or excessive cable length |
| Errors counter climbing during echo | the far end is faster than the RX buffer drains — raise the baud or shorten bursts |

## Echo mode

**Start echo** spawns a task that reads the test UART and writes everything
straight back out. Wire a USB-serial adapter to the two pins (plus ground),
type in a terminal, and you should see your own characters come back.

Echo mode and the loopback tests are mutually exclusive — echo would eat the
reply. Running a test while echo is active returns:

```
stop echo mode before running a loopback test
```

## API

```c
#include "bsp/bsp_uart_test.h"

bsp_uart_test_init();                       // install the driver (idempotent)
bsp_uart_test_set_baud(230400);

bsp_uart_test_result_t r;
bsp_uart_test_run(BSP_UART_TEST_EXTERNAL, &r);
bsp_uart_test_log(&r);                      // one-line PASS/FAIL to the console
if (r.pass) { /* ... */ }

bsp_uart_echo_start();
bsp_uart_test_send("hello\r\n");

uint32_t tx, rx, errs;
bsp_uart_test_stats(&tx, &rx, &errs);
```

`bsp_uart_test_run()` returns `ESP_OK` whenever the transport worked; a data
mismatch shows up as `result.pass == false`, not as an error code. Transport
failures (driver not installed, echo mode active) come back in `result.err`.
