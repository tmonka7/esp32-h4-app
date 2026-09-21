# JC8012P4A1 hardware reference

Guition JC8012P4A1 — ESP32-P4NRW32 (400 MHz dual-core RISC-V, 32 MB PSRAM,
16 MB flash) + ESP32-C6-MINI-1U-N4, 10.1" 800×1280 IPS with MIPI-DSI.

Everything below was read off the vendor schematics
(`5-Schematic/1_PWR.png`, `3_ESP32-P4.png`, `4_CONN.png`, `7_CODEC.png` in the
Guition package) and cross-checked against the vendor's own ESP-IDF BSP
(`1-Demo/idf-examples/common_components/esp32_p4_function_ev_board/`).

Where a value could not be confirmed from both sources it is flagged.

---

## Pin map

### I²C0 — shared control bus

| Signal | GPIO | Notes |
|---|---|---|
| SDA | 7 | net `ES_I2C_SDA` / `RTC_DAT/SDA1` |
| SCL | 8 | net `ES_I2C_SCL` / `RTC_CLK/SCL1` |

400 kHz. The board fits 5k1 pull-ups (R53/R54), so the internal pull-ups are
left off.

Devices on the bus:

| Device | 7-bit address | Notes |
|---|---|---|
| GSL3680 touch controller | 0x40 | firmware is downloaded over I²C on every power-up |
| ES8311 audio codec | 0x18 | `ES8311_CODEC_DEFAULT_ADDR` is the 8-bit form, 0x30 |
| RX8025T RTC | 0x32 | backed by CR1220 cell BT1 |
| CSI camera sensor | varies | connector FPC5, no sensor fitted from the factory |

### Display — JD9365, MIPI-DSI

| Signal | GPIO / value |
|---|---|
| Panel reset | GPIO27 |
| Backlight PWM | GPIO23 (LEDC, 20 kHz, active high) |
| Resolution | 800 × 1280 (portrait) |
| Data lanes | 2 |
| Lane bit rate | 1500 Mbps |
| DPI clock | 60 MHz |
| Pixel format | RGB565 |
| DSI PHY supply | on-chip LDO channel 3 @ 2500 mV |

Video timing used by this firmware (matches the vendor BSP):

```
hsync_pulse_width 20   hsync_back_porch 20   hsync_front_porch 40
vsync_pulse_width  4   vsync_back_porch  8   vsync_front_porch 20
```

#### Panel revision V2

Units from batch **2624** onwards carry a different panel, marked
`JC8012P4A1-V2`. Check the sticker on the back. Its parameters differ:

```
DPI clock        70 MHz
lane bit rate    840 Mbps
hsync_pulse_width 20   hsync_back_porch 20   hsync_front_porch 40
vsync_pulse_width  4   vsync_back_porch 10   vsync_front_porch 30
```

and it needs its own JD9365DA init sequence (a long page-switched register
dump starting `E0 00 / E1 93 / E2 65 / E3 F8 / 80 01`). This firmware ships the
original timings only. If your board is V2 and the display stays blank or
scrambled, that is the cause; the full V2 init sequence is published in the
ESPHome device package for `guition-esp32-p4-jc8012p4a1-v2`.

### Touch — GSL3680 (Silead)

| Signal | GPIO |
|---|---|
| Reset | 22 |
| Interrupt | 21 |

Not a GT911, despite what several parts listings claim. The driver downloads a
firmware blob over I²C during `esp_lcd_touch_new_i2c_gsl3680()`, which takes a
few hundred milliseconds at boot.

Default orientation flags in this firmware — `swap_xy = 0`, `mirror_x = 0`,
`mirror_y = 1` — match the vendor ESP-IDF BSP.

### TF card — SDMMC slot 0

| Signal | GPIO |
|---|---|
| CLK | 43 |
| CMD | 44 |
| D0 | 39 |
| D1 | 40 |
| D2 | 41 |
| D3 | 42 |

These are IOMUX-fixed for slot 0 on the ESP32-P4, which is why they match the
official P4 EV board exactly. Card VDD is `ESP_LDO_VO4`, so the host needs an
`sd_pwr_ctrl` handle on **LDO channel 4** or the card never leaves reset.
External pull-ups R47–R52 (5k1) are fitted.

### Audio — ES8311 + NS4150

| Signal | GPIO |
|---|---|
| I²S MCLK | 13 |
| I²S BCLK (SCLK) | 12 |
| I²S WS (LRCK) | 10 |
| I²S DOUT (P4 → codec) | 9 |
| I²S DIN (codec → P4) | 11 |
| PA_CTRL (NS4150 enable) | 20 |

The ES7210 nets on the schematic share MCLK/SCLK/LRCK; only the ES8311 is
populated on this board. Speaker connector is CN3. An MSM381A3729H9CP analogue
microphone feeds ADC_MIC1_P/N.

### Power and battery

- **IP5306** power-path / charger, Li-ion connector **CN1** (BAT+ / BAT−).
- BAT+ → **R2 68 kΩ** → node → **R6 100 kΩ** → GND; node → R4 (0 Ω) → **GPIO52**.
- GPIO52 is **ADC2 channel 3** on the ESP32-P4.
- So `V_bat = V_adc × (68 + 100) / 100 = V_adc × 1.68`.
  A full cell at 4.2 V reads 2.5 V at the pin, inside the 12 dB (≈3.1 V) range.
- 3V3 rail from a TLV62569 off `VOUT-BAT`; 1V2 core rail from a second TLV62569.

The IP5306 variant here has no I²C and its LED1–LED3 pins are not connected to
the ESP32-P4, so **charge/discharge state cannot be read in software**. Only
the terminal voltage is observable.

### UART

| Port | TX | RX | Use |
|---|---|---|---|
| UART0 | GPIO37 | GPIO38 | ESP-IDF console. Wired to the CH340 **and** to header CN2. |
| UART1 (this firmware) | GPIO45 | GPIO46 | loopback test / echo, on FPC3. Configurable. |

**CN2** is a 4-pin MX1.25: `1 = VIN, 2 = TXD (UART0_TXD), 3 = RXD (UART0_RXD),
4 = GND`. Series resistors R56/R57 (0 Ω) and a 47 k pull-down R58 on RX.

### ESP32-C6 co-processor — SDIO slot 1

| Signal | GPIO |
|---|---|
| CLK | 18 |
| CMD | 19 |
| D0 | 14 |
| D1 | 15 |
| D2 | 16 |
| D3 | 17 |
| Reset | 54 |

Runs at 40 MHz in the vendor firmware. Not driven by this build; the pins are
recorded in `bsp/jc8012p4a1.h` so they do not get reused by accident.

### Expansion headers

| Header | Signals |
|---|---|
| FPC3 (16-pin) | GPIO45, 46, 47, 48, 5, 4, 3, 2, ES_I2C_SCL, ES_I2C_SDA, VDDA, GND |
| FPC4 (16-pin) | GPIO34, 33, 32, 31, 30, 29, 28, VDDA, GND |
| FPC2 (6-pin) | TOUCH_RST, TOUCH_INT, RTC_CLK/SCL1, RTC_DAT/SDA1, 3V3, GND |
| FPC5 (15-pin) | MIPI-CSI: 2 data lanes + clock, CSI_IO0, CSI_IO1, I²C |
| CN4 (6-pin) | ES_I2C_SCL, ES_I2C_SDA, 3V3, GND |

The UART loopback test defaults to GPIO45/GPIO46 because both are free on FPC3
and adjacent, so a single jumper links them.

### Other

| Item | GPIO |
|---|---|
| WS2812 RGB LED | 26 |
| Boot mode strap | 35 |
| Pull-up to 3V3 | 36 (10 k) |
| Flash (W25Q128) | dedicated SPI pins |

---

## Cross-checks and confidence

| Claim | Confirmed by |
|---|---|
| I²C, touch, backlight, LCD reset, SDIO, LDO channels | schematic **and** vendor BSP **and** ESPHome device package |
| SD card pins | vendor BSP; also IOMUX-fixed for P4 slot 0, so not board-dependent |
| I²S / codec pins | schematic sheet 7 **and** vendor BSP |
| Battery divider on GPIO52 | schematic sheet 1 only — verify with a meter before trusting the percentage |
| UART0 on GPIO37/38 | schematic sheet 3 **and** the ESP-IDF v5.3 default for esp32p4 |
| Touch is GSL3680, not GT911 | vendor driver source, ESPHome package, schematic net names |
