# JC8012P4A1_BSP_ESP32P4

Smart Control Panel firmware for the **Guition JC8012P4A1** — ESP32-P4NRW32 +
ESP32-C6, 10.1" 800×1280 MIPI-DSI panel, GSL3680 capacitive touch.

Built against **ESP-IDF v5.3** (also builds on v5.4; not v5.5, because the
vendored `esp_wifi_remote` 0.4.1 only ships v5.3 / v5.4 Wi-Fi headers). **Builds fully
offline** — every dependency is committed under `components/`, and there is no
`idf_component.yml` anywhere in the tree, so the component manager never
contacts `components.espressif.com`.

> **Not yet compiled.** There is no ESP-IDF toolchain on the machine this was
> written on, so the code has never been through a compiler. Every API used
> was checked field-by-field against the ESP-IDF v5.3 headers and against the
> vendored component sources, but expect to fix a few things on the first
> build. See [Status](#status).

---

## Quick start

```bash
# once, from an ESP-IDF v5.3 shell
idf.py set-target esp32p4
idf.py build
idf.py -p COM5 flash monitor        # or /dev/ttyUSB0
```

ESP32-P4 is a fully supported target in v5.3 — no `--preview` flag needed.

The console is UART0 at 115 200 baud on **GPIO37 (TX) / GPIO38 (RX)**, which is
wired both to the on-board CH340 and to the 4-pin MX1.25 header **CN2**
(1 = VIN, 2 = TXD, 3 = RXD, 4 = GND).

---

## What the firmware does

Thirteen screens behind a status bar and a bottom navigation strip, laid out
for landscape 1280×800 (the panel is physically portrait 800×1280; LVGL rotates
90° in software — see `CONFIG_BSP_DISPLAY_ROTATION_*`).

| Screen | What it actually does |
|---|---|
| **Home** | Live clock from the RX8025T/system clock, launcher grid |
| **Battery** | Real ADC reading of the Li-ion divider on GPIO52, 30 s voltage chart, SoC temperature |
| **Timer** | Real countdown driven by `esp_timer`, beeps through the ES8311 at zero |
| **Video Player** | Plays a directory of numbered baseline JPEG frames at 10 fps (see [note](#video)) |
| **Music** | Real 16-bit PCM WAV playback from the TF card through the ES8311 + NS4150 |
| **Photo** | Real JPEG / PNG / BMP decode from the TF card via LVGL's decoders |
| **SD Card** | Real FAT volume: card name, used/total, root listing |
| **File Manager** | Real directory browsing of `/sdcard` |
| **Camera** | Probes the CSI control bus and reports honestly that no sensor is fitted |
| **Settings** | Backlight PWM, codec volume, 1 kHz test tone, RTC state, card remount, restart |
| **Widgets** | Live heap, PSRAM, uptime, SoC temperature, task count, backlight |
| **About** | Chip revision, flash and PSRAM size, base MAC, IDF and LVGL versions, build date |
| **UART** | TX/RX loopback tests, echo mode, live byte counters — see below |

Where something genuinely cannot be measured on this board, the UI says so
rather than showing a plausible number. Two examples:

- **Charge state.** The IP5306 fitted here is the button/LED variant with no
  I²C, and its LED1–LED3 outputs are not wired to the ESP32-P4. Only the
  terminal voltage is observable, so that is all the Battery screen claims.
- **Wi-Fi / Bluetooth.** Both radios live on the ESP32-C6 co-processor, which
  this firmware does not bring up. The status-bar icons stay dimmed.

---

## UART TX/RX verification

This was the specific thing you asked to have checked, so it gets its own
screen and its own boot-time test.

**UART0 is the console** and is deliberately left alone. Reconfiguring it for a
loopback test while the console driver owns it produces meaningless results —
and if you are reading boot logs at all, UART0 TX is already proven.

The tests therefore run on a **second port** (default UART1) on free pins of
the FPC3 expansion header:

| | Default | Kconfig |
|---|---|---|
| Port | UART1 | `CONFIG_BSP_UART_TEST_PORT` |
| TX | GPIO45 | `CONFIG_BSP_UART_TEST_TX_GPIO` |
| RX | GPIO46 | `CONFIG_BSP_UART_TEST_RX_GPIO` |
| Baud | 115200 8N1 | `CONFIG_BSP_UART_TEST_BAUD` |

Two tests, both sending a 64-byte pattern (alternating bits, all-ones,
all-zeros, walking bits, ASCII, a ramp) and comparing what comes back:

- **Internal** — `uart_set_loop_back()`, TX looped to RX inside the peripheral.
  No wiring needed. Proves the port, driver, clock and baud divisor. Runs
  automatically at boot (`CONFIG_BSP_UART_TEST_ON_BOOT`) and logs PASS/FAIL.
- **External** — the real check. Put a jumper between GPIO45 and GPIO46 on
  FPC3, then press the button. A pass means the pads and board wiring are good.

Also on that screen: **echo mode** (everything on RX goes straight back out
TX), a **send test string** button, a baud-rate selector, and running
TX/RX/error byte counters.

Console output looks like:

```
I (1234) bsp_uart: UART1 ready: TX=GPIO45 RX=GPIO46 @ 115200 baud 8N1
I (1240) bsp_uart: UART1 internal loopback PASS - TX=GPIO45 RX=GPIO46 @ 115200 baud,
                   64/64 bytes returned intact in 812 us
```

Full detail: [`docs/UART_TEST.md`](docs/UART_TEST.md).

---

## Hardware

Pin assignments were taken from the vendor schematics (`5-Schematic/*.png` in
the Guition package) and cross-checked against the vendor's own ESP-IDF BSP.
Full map and provenance: [`docs/HARDWARE.md`](docs/HARDWARE.md).

Summary:

| Block | Pins |
|---|---|
| I²C0 (touch 0x40, codec 0x18, RTC 0x32) | SDA GPIO7, SCL GPIO8, 400 kHz |
| Display JD9365 MIPI-DSI | RST GPIO27, 2 lanes @ 1500 Mbps, DPI 60 MHz, PHY from LDO ch3 @ 2.5 V |
| Backlight | GPIO23, LEDC PWM 20 kHz |
| Touch GSL3680 | RST GPIO22, INT GPIO21 |
| TF card (SDMMC slot 0) | CLK 43, CMD 44, D0–D3 39–42, VDD from LDO ch4 |
| Audio ES8311 + NS4150 | MCLK 13, BCLK 12, WS 10, DOUT 9, DIN 11, PA_CTRL 20 |
| Battery sense | GPIO52 = ADC2 ch3, divider 68 k / 100 k |
| Console UART0 | TX 37, RX 38 |
| ESP32-C6 (SDIO slot 1, ESP-Hosted) | CLK 18, CMD 19, D0–D3 14–17, RST 54 |
| WS2812 LED | GPIO26 |

---

## Offline build

`components/` holds every dependency, pinned:

| Component | Version | Source |
|---|---|---|
| `lvgl` | 9.2.2 | ESP Component Registry |
| `esp_lvgl_port` | 2.4.4 | ESP Component Registry |
| `esp_lcd_touch` | 1.1.2 | ESP Component Registry |
| `esp_codec_dev` | 1.3.4 | ESP Component Registry |
| `cmake_utilities` | 0.5.3 | ESP Component Registry |
| `esp_hosted` | 0.0.27 | ESP Component Registry, host side only, 1 local patch |
| `esp_wifi_remote` | 0.4.1 | ESP Component Registry |
| `esp_lcd_jd9365` | 1.0.2 | Guition vendor package |
| `esp_lcd_touch_gsl3680` | vendor | Guition vendor package |
| `bsp_jc8012p4a1` | this repo | written for this board |

Every `idf_component.yml` was removed, so the component manager finds nothing
to resolve and stays off the network. The top-level `CMakeLists.txt` also sets
`IDF_COMPONENT_MANAGER=0` as a second line of defence.

To refresh the vendored tree on a networked machine:
`tools/vendor_components.sh` (or `.ps1`). Details and the LVGL pruning caveat:
[`docs/OFFLINE_BUILD.md`](docs/OFFLINE_BUILD.md).

---

## Status

**Written and reviewed, not compiled.** No ESP-IDF toolchain was available, so
treat the first `idf.py build` as the real test. What *was* verified:

- Every ESP-IDF API used (`esp_ldo_acquire_channel`, `esp_lcd_new_dsi_bus`,
  `esp_lcd_dpi_panel_config_t`, `sd_pwr_ctrl_new_on_chip_ldo`,
  `esp_vfs_fat_info`, `uart_set_loop_back`, the ADC oneshot/cali API, …)
  checked field-by-field against the `release/v5.3` headers.
- Every LVGL call checked against the vendored LVGL 9.2.2 headers.
- Every `CONFIG_*` symbol checked to exist for `esp32p4` on v5.3, and unset
  Kconfig booleans wrapped so they never appear bare in a C expression.
- All BSP header declarations matched against definitions; all screen
  callbacks matched against their implementations.

Known deliberate limitations, not bugs:

<a name="video"></a>
- **Video.** There is no video codec in this build. ESP-IDF v5.3 predates the
  ESP32-P4 hardware JPEG driver (v5.4) and no container parser is vendored.
  The Video screen plays a numbered JPEG frame sequence instead, which
  exercises the same decode-and-blit path a real player would.
- **Camera.** Capture needs `esp_cam_sensor` + `esp_video`, which are not
  vendored, and the board ships without a sensor on the CSI connector.
- **Wi-Fi is station-only and configured at build time.** The P4 is the
  ESP-Hosted *host*; the C6 keeps the ESP-Hosted-MCU slave firmware it ships
  with (`JC8012P4A1_C6.bin` in the vendor package; reflash that if the C6 was
  overwritten). Set the network under `menuconfig > Wi-Fi (ESP32-C6 via
  ESP-Hosted)`. There is no on-device network picker yet, and Bluetooth is
  not brought up.
- **TF card and Wi-Fi share one SDMMC controller** (slot 0 and slot 1). On
  v5.3 the controller can only be torn down as a whole, so unmounting the card
  leaves it running; `tools/patches/` makes ESP-Hosted accept a controller the
  card already initialised.
- **Cam Remote needs a network.** The Cam Remote screen shows a network
  camera's multipart MJPEG stream (`GET http://<camera>:81/stream`, as served
  by ESP32-CAM CameraWebServer), decoded with LVGL's TJpgDec (baseline JPEG,
  up to 1920x1080). The URL defaults to `CONFIG_APP_CAM_STREAM_URL` and can be
  edited on the device (kept in NVS). Until Wi-Fi has an IP address it
  reports the Wi-Fi state instead of opening a socket.
- **Software rotation costs frame rate.** Landscape needs a 90° software
  rotate of every flushed area. ESP-IDF v5.4+ can offload this to the P4's PPA;
  v5.3 cannot. If you want maximum speed on v5.3, choose
  `CONFIG_BSP_DISPLAY_ROTATION_0` and re-lay-out the screens for portrait.

### Things to check first on real hardware

1. **Touch orientation.** `CONFIG_BSP_TOUCH_MIRROR_Y=y` by default, matching
   the vendor BSP. If touches land in the wrong place, flip the
   `CONFIG_BSP_TOUCH_*` options rather than patching code.
2. **Panel revision.** Boards in batch 2624 and later ship a different panel
   (marked `JC8012P4A1-V2`) that wants a different init sequence and 70 MHz
   DPI / 840 Mbps lanes. This build uses the original JD9365 timings. If the
   display is blank or scrambled, that is the first thing to suspect —
   `docs/HARDWARE.md` has the V2 parameters.
3. **ADC2 availability.** Battery sense uses ADC2 channel 3. If
   `bsp_power_init()` fails, the Battery screen degrades gracefully and the
   status bar shows `--`.

---

## Licence and attribution

Code written for this repository is Apache-2.0.

Vendored third-party components keep their own licences, which are included
in their directories:

- LVGL — MIT
- `esp_lvgl_port`, `esp_lcd_touch`, `esp_codec_dev`, `cmake_utilities`,
  `esp_lcd_jd9365` — Apache-2.0, Espressif Systems
- `esp_lcd_touch_gsl3680` — from the Guition JC8012P4A1 vendor package
  (derived from Silead reference code); check the vendor package before
  redistributing.
