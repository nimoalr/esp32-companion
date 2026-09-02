# Hardware notes: Waveshare ESP32-S3-Touch-AMOLED-1.75

Board: Waveshare ESP32-S3-Touch-AMOLED-1.75, SKU 31261, and the 31262 "-B" variant
(same board supplied with a case). MCU ESP32-S3R8 (8 MB octal PSRAM), 16 MB QIO flash.

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
* The BSP mirrors X and Y for LVGL. Tap detection only needs travel distance,
  so this firmware reads raw coordinates and applies no mirroring.
* The driver reads the report register with retries and a 2 ms settle delay,
  so each poll costs a few milliseconds of the touch task's time on core 0.

## Power: AXP2101

The panel connector J3 is fed from `VCC3V3`, which the schematic labels as the
AXP2101 **DCDC1** output; the same rail powers the ESP32-S3. Neither the
maintained BSP nor the Arduino display examples write to the AXP2101 to bring
the display up, so this firmware does not talk to the PMIC at all (address
`0x34` is never addressed). The other AXP2101 outputs (ALDO1..4, BLDO1..2,
DCDC2..4, CPUSLDO) feed audio, RTC and sensor circuits that this demo does not
use, and are left in their power-on state.

## Things deliberately not touched

ES8311/ES7210 audio, NS4150B amplifier (GPIO46), QMI8658 IMU, PCF85063 RTC,
TCA9554 expander (0x20), SD card (GPIO1/2/3/41), USB, Wi-Fi, Bluetooth, NVS,
sleep modes. Their GPIOs are never configured.
