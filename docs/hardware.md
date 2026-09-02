# Hardware notes: Waveshare ESP32-S3-Touch-AMOLED-1.75

Board: Waveshare ESP32-S3-Touch-AMOLED-1.75, SKU 31261, and the 31262 "-B" variant
(same board supplied with a case). MCU ESP32-S3R8 (8 MB octal PSRAM), 16 MB QIO flash.
Battery: the 3.7 V 500 mAh MX1.25 pouch sold for the board.

## Sources

| Tag | Source |
| --- | --- |
| HWREF | `HARDWARE_REFERENCE.md` in `waveshareteam/ESP32-S3-Touch-AMOLED-1.75` (commit `e4344e7`) |
| BSP | `waveshare/esp32_s3_touch_amoled_1_75` v3.0.1 on the Espressif component registry (`include/bsp/esp32_s3_touch_amoled_1_75.h`, `esp32_s3_touch_amoled_1_75.c`); the same files are vendored in the Waveshare repo under `firmware/brookesia/components/` |
| SCH | `Schematic/ESP32-S3-Touch-AMOLED-1.75-schematic.pdf` in the Waveshare repo (net names quoted below) |
| ARD | `examples/arduino/libraries/Mylibrary/pin_config.h` in the Waveshare repo |

Every pin used by this firmware agrees across all four sources.

## Pin map

| Signal | GPIO | HWREF | BSP macro | SCH net | ARD macro |
| --- | ---: | --- | --- | --- | --- |
| QSPI clock | 38 | `LCD_PCLK` | `BSP_LCD_PCLK` | `QSPI_SCL` | `LCD_SCLK` |
| QSPI data 0 | 4 | `LCD_DATA0` | `BSP_LCD_DATA0` | `QSPI_SIO0` | `LCD_SDIO0` |
| QSPI data 1 | 5 | `LCD_DATA1` | `BSP_LCD_DATA1` | `QSPI_SI1` | `LCD_SDIO1` |
| QSPI data 2 | 6 | `LCD_DATA2` | `BSP_LCD_DATA2` | `QSPI_SI2` | `LCD_SDIO2` |
| QSPI data 3 | 7 | `LCD_DATA3` | `BSP_LCD_DATA3` | `QSPI_SI3` | `LCD_SDIO3` |
| Panel chip select | 12 | `LCD_CS` | `BSP_LCD_CS` | `LCD_CS` | `LCD_CS` |
| Panel reset | 39 | `LCD_RST` | `BSP_LCD_RST` | `LCD_RESET` | `LCD_RESET` |
| Panel tearing effect | 13 | `LCD_TE` | not used by the BSP | `LCD_TE` (J3 pin, routed to GPIO13) | not used |
| I2C SCL | 14 | `I2C_SCL` | `BSP_I2C_SCL` | `TP_SCL` / `ESP32_SCL` | `IIC_SCL` |
| I2C SDA | 15 | `I2C_SDA` | `BSP_I2C_SDA` | `TP_SDA` / `ESP32_SDA` | `IIC_SDA` |
| Touch interrupt | 11 | `TP_INT` | `BSP_LCD_TOUCH_INT` | `TP_INT` | `TP_INT` |
| Touch reset | 40 | `TP_RST` | `BSP_LCD_TOUCH_RST` | `TP_RESET` | `TP_RESET` |

Panel and touch have separate reset lines; they are never treated as shared.

## Display: CO5300, 466 x 466, QSPI, RGB565

* SPI host `SPI2_HOST` (BSP `BSP_LCD_SPI_NUM`), quad mode, 32-bit command phase,
  8-bit parameters. Pixel writes use opcode `0x32`, commands `0x02` (handled by
  `espressif/esp_lcd_co5300`).
* Init sequence: copied verbatim from the BSP `lcd_init_cmds[]`. It sets COLMOD
  `0x55` (RGB565), enables TE with `0x35 0x00` (V-blank pulse only), brightness
  `0x51 0xFF`, and the address window `0x2A 0x0006..0x01D7`.
* Column offset: the BSP calls `esp_lcd_panel_set_gap(panel, 0x06, 0)`; the
  visible 466 columns start at GRAM column 6. This firmware does the same.
* Address granularity: the BSP's LVGL rounder forces even start coordinates and
  odd end coordinates on both axes. Dirty rectangles here are expanded to
  2-pixel alignment before being pushed.
* Byte order: the QSPI panel takes the high RGB565 byte first, so pixel buffers
  hold big-endian RGB565. The coverage LUT is built pre-swapped, so no swap
  happens on the pixel path.
* Brightness is a panel command (`0x51`); there is no backlight GPIO.

### QSPI clock

Default: **40 MHz**, `CONFIG_EYES_LCD_PCLK_40M`. That is the value every
Waveshare reference program uses (`CO5300_PANEL_IO_QSPI_CONFIG` sets
`pclk_hz = 40 MHz`, and the BSP does not override it). An **80 MHz** option
(`CONFIG_EYES_LCD_PCLK_80M`, `idf.py menuconfig` -> "Eyes demo") is provided
but was not verified on a panel while this project was written. If the image is
corrupt at 80 MHz, go back to 40 MHz.

Bus time per frame, RGB565 (bits per clock = 4):

| Payload | Bytes | 40 MHz | 80 MHz |
| --- | ---: | ---: | ---: |
| Full frame 466 x 466 | 434 312 | 21.7 ms | 10.9 ms |
| Two neutral eyes (2 x 111 x 141 px) | 62 604 | 3.1 ms | 1.6 ms |
| Two "surprised" eyes (2 x 145 x 183 px) | 106 140 | 5.3 ms | 2.7 ms |

At 60 Hz the frame budget is 16.7 ms, so idle frames fit at either clock.

### Tearing effect (TE)

TE **is wired**: CO5300 TE -> J3 -> `LCD_TE` -> GPIO13 (HWREF and SCH). The
firmware enables TE in the init sequence and takes a rising-edge interrupt on
GPIO13. Each frame is rasterised up to the first band, then the panel write
starts on the next TE edge. Because the write proceeds top to bottom faster than
the panel's own scan (e.g. 52 rows/ms for a 190 px wide rect at 40 MHz versus
about 28 rows/ms for the scan) and starts at V-blank, the scan never crosses a
half-written row, so blinks do not tear.

Fallback: if no TE edges arrive during the first 100 ms after init, the
firmware logs a warning and paces frames with a 60 Hz `esp_timer` instead.

## Touch: CST9217 over I2C

* 7-bit address `0x5A` (HWREF, driver header). Shared bus at 400 kHz (HWREF:
  "the maintained BSP defaults to a 400 kHz I2C clock").
* Driver: `waveshare/esp_lcd_touch_cst9217` v2.0.0 on top of
  `espressif/esp_lcd_touch` v1.2.1. Interrupt on the falling edge of GPIO11
  (`levels.interrupt = 0`, as in the BSP), reset active low on GPIO40.
* The BSP mirrors X and Y for LVGL; this firmware does the same
  (`EYES_TOUCH_MIRROR_X/Y`, both on by default) so swipe directions match the
  screen. Not yet verified on hardware; flip the options if they come out
  reversed.
* The driver reads the report register with retries and a 2 ms settle delay,
  so each poll costs a few milliseconds of the touch task's time on core 0.

## Power: AXP2101

The panel connector J3 is fed from `VCC3V3`, which the schematic labels as the
AXP2101 **DCDC1** output; the same rail powers the ESP32-S3. Neither the
maintained BSP nor the Arduino display examples write to the AXP2101 to bring
the display up, so no rail is ever changed by this firmware. The other AXP2101
outputs (ALDO1..4, BLDO1..2, DCDC2..4, CPUSLDO) feed audio, RTC and sensor
circuits and are left in their power-on state.

What the firmware does use (address `0x34`, register names as in XPowersLib):

| Register | Use |
| --- | --- |
| `0x03` IC type | probe, expects `0x4A` |
| `0x30` ADC channel control | battery voltage ADC on; TS (thermistor) measurement off, as in Waveshare's AXP2101 example for this board, because the supplied pouch has no NTC and an open TS input stops the charger |
| `0x00` / `0x01` status | battery present, VBUS good, charging state |
| `0x34` / `0x35`, `0xA4` | battery voltage (mV) and fuel-gauge percent |
| `0x10` common config, bit 0 | soft power off (DEEP state) |

Power paths, from the schematic:

* AXP2101 **PWROK -> ESP32-S3 CHIP_PU**: the PMIC holds the MCU in reset until
  the rails are up, and a PMIC power-off removes the MCU's power entirely.
* PWR button (Key1) **-> AXP2101 PWRON**: the hardware power-on path. A press
  of the configured on-time (default 128 ms) starts the PMIC; holding it for
  the off-time (default 6 s) forces a power off with no firmware involved.
  Plugging in USB also powers the PMIC on. The conditioned button state
  (`SYS_OUT`) and the PMIC IRQ only reach the TCA9554 expander, not an ESP32
  GPIO, so the PWR button cannot wake the ESP32 from sleep; it can only restart
  a powered-off board.
* **BOOT (GPIO0)** is a strapping pin: if it is low when the ROM samples it
  after a reset or deep-sleep wake, the chip enters download mode. It is not
  used as a wake button for that reason.

## Motion: QMI8658

Address `0x6B`. INT2 is wired to **GPIO21**, an RTC-capable pin, which is what
makes wake-on-motion usable as a light- and deep-sleep wake source. INT1 only
reaches the TCA9554 expander. Awake, the accelerometer runs at +-8 g, 62.5 Hz
with the low-pass filter on and is polled every 50 ms (200 ms in DROWSY). For
SLEEP the chip is reset into its hardware wake-on-motion mode (low-power 21 Hz
accelerometer, threshold `EYES_WOM_MG`, INT2 idles low and goes high on
motion); normal data output is unavailable in that mode, so the chip is reset
back to polling mode on wake.

Calibration (setup UI, first boot): three poses, 40 still samples each. In
each pose one axis reads about +1 g and the other two read their bias; the
bias of an axis is the mean of its readings in the two poses where it is
horizontal, the scale comes from the pose where it is vertical (+-15 %
tolerance, otherwise the pose is rejected). Because the accelerometer reads
"up", the flat pose gives the out-of-screen direction, the upright pose the
screen-up direction and the left-edge pose the screen-right direction; after
Gram-Schmidt these form the sensor-to-screen rotation stored with the
calibration. Everything downstream (level screen, future motion features)
works in screen coordinates, so the sensor's mounting orientation never has
to be hard-coded.

## Sleep and wake plumbing

* DROWSY uses ESP-IDF power management: `esp_pm_configure` with the CPU between
  40 MHz and `EYES_CPU_MAX_MHZ`, automatic light sleep when no lock is held.
  The render task holds `ESP_PM_NO_LIGHT_SLEEP` from just before the TE wait
  until the frame's DMA is done, and releases it while waiting for the next
  frame slot. The SPI and I2C drivers hold their own APB locks during
  transfers.
* SLEEP is a manual `esp_light_sleep_start` with three wake sources: GPIO21
  high (IMU), GPIO11 low (touch INT) and a timer for the DEEP deadline.
  `gpio_wakeup_enable` switches a pin to level interrupts, so the touch pin's
  edge interrupt is disabled before sleep and restored afterwards.
* DEEP (default) writes the AXP2101 soft power-off bit after Display Off +
  Sleep In. Alternative (`EYES_DEEP_SLEEP`): `esp_deep_sleep_start` with EXT1
  wake on GPIO21 high; the rails stay up, so the board draws far more.

## Battery budget (500 mAh pouch)

Estimated draws; the ESP32 numbers are from the datasheet, the board-level
additions (panel, PSRAM, codecs at idle, PMIC quiescent) are estimates until
measured. Runtime assumes 85 % of nominal capacity is usable.

| State | Board draw (est.) | Runtime on 500 mAh |
| --- | ---: | ---: |
| ACTIVE, eyes at 60 fps, 100 % brightness | ~75 mA | ~5.7 h |
| DROWSY, 10 % brightness, 15 fps, light sleep between frames | ~5 mA | ~3.5 days |
| SLEEP, panel off, light sleep, IMU wake-on-motion | ~2 mA | ~9 days |
| DEEP as ESP32 deep sleep (rails on) | ~0.7 mA | ~25 days |
| DEEP as PMIC off | ~0.04 mA | limited by self-discharge (>1 year) |

A day with one hour of active use and the rest asleep is about 120 mAh, so
roughly 3.5 days per charge. Formula: `hours = mAh x 0.85 / mA`.

## Microphones: ES7210

Only used by the dance mode; the I2S peripheral and the codec are brought up
when the expression is entered and torn down when it is left.

| Signal | GPIO | Source |
| --- | ---: | --- |
| I2S MCLK | 42 | HWREF, BSP `BSP_I2S_MCLK` |
| I2S BCLK | 9 | HWREF, BSP `BSP_I2S_SCLK` |
| I2S LRCK / WS | 45 | HWREF, BSP `BSP_I2S_LCLK` |
| I2S data in (ES7210 SDOUT) | 10 | HWREF, BSP `BSP_I2S_DSIN` |

ES7210 at 7-bit address `0x40` (esp_codec_dev takes the 8-bit form `0x80`).
MIC1 and MIC2 are the two front microphones; the firmware selects exactly
those two, which keeps the codec in standard 2-channel I2S rather than the
4-slot TDM the Waveshare BSP uses for its echo-reference path. Sample format
16 kHz, 16-bit stereo, the same as Waveshare's spectrum-analyser example. The
ES7210 is an I2S slave and needs MCLK; the I2S driver generates it at 256 x fs.
Microphone gain is `EYES_AUDIO_GAIN_DB` (30 dB default, as in the Waveshare
example). The ES8311 playback codec and the NS4150B amplifier are not used.

## Things deliberately not touched

ES8311 playback, NS4150B amplifier (GPIO46), PCF85063 RTC, TCA9554 expander
(0x20), SD card (GPIO1/2/3/41), USB, Wi-Fi, Bluetooth. Their GPIOs are never
configured.
