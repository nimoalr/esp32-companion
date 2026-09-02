/*
 * Board facts for the Waveshare ESP32-S3-Touch-AMOLED-1.75 (SKU 31261 / 31262 "-B").
 *
 * Every value below carries the source it was taken from:
 *   [HWREF]  waveshareteam/ESP32-S3-Touch-AMOLED-1.75, HARDWARE_REFERENCE.md
 *   [BSP]    waveshare/esp32_s3_touch_amoled_1_75 v3.0.1 (component registry),
 *            include/bsp/esp32_s3_touch_amoled_1_75.h and esp32_s3_touch_amoled_1_75.c
 *   [SCH]    Schematic/ESP32-S3-Touch-AMOLED-1.75-schematic.pdf in the same repo
 *   [ARD]    examples/arduino/libraries/Mylibrary/pin_config.h in the same repo
 * See docs/hardware.md for the cross-check table.
 */
#pragma once

#include "driver/gpio.h"

/* ---- CO5300 AMOLED over QSPI --------------------------------------------- */
#define BOARD_LCD_SPI_HOST     SPI2_HOST        /* [BSP] BSP_LCD_SPI_NUM */
#define BOARD_LCD_PCLK         GPIO_NUM_38      /* [HWREF][BSP][SCH QSPI_SCL][ARD] */
#define BOARD_LCD_DATA0        GPIO_NUM_4       /* [HWREF][BSP][SCH QSPI_SIO0][ARD] */
#define BOARD_LCD_DATA1        GPIO_NUM_5       /* [HWREF][BSP][SCH QSPI_SI1][ARD] */
#define BOARD_LCD_DATA2        GPIO_NUM_6       /* [HWREF][BSP][SCH QSPI_SI2][ARD] */
#define BOARD_LCD_DATA3        GPIO_NUM_7       /* [HWREF][BSP][SCH QSPI_SI3][ARD] */
#define BOARD_LCD_CS           GPIO_NUM_12      /* [HWREF][BSP][SCH LCD_CS][ARD] */
#define BOARD_LCD_RST          GPIO_NUM_39      /* [HWREF][BSP][SCH LCD_RESET][ARD] */
#define BOARD_LCD_TE           GPIO_NUM_13      /* [HWREF][SCH LCD_TE]; not used by [BSP] */

#define BOARD_LCD_H_RES        466              /* [BSP] BSP_LCD_H_RES */
#define BOARD_LCD_V_RES        466              /* [BSP] BSP_LCD_V_RES */
#define BOARD_LCD_X_GAP        0x06             /* [BSP] esp_lcd_panel_set_gap(panel, 0x06, 0); CASET 0x0006..0x01D7 in init */
#define BOARD_LCD_Y_GAP        0
#define BOARD_LCD_PIXEL_ALIGN  2                /* [BSP] rounder callback: even start, odd end, on both axes */

/* ---- CST9217 touch over the shared I2C bus -------------------------------- */
#define BOARD_I2C_PORT         0                /* [BSP] CONFIG_BSP_I2C_NUM default */
#define BOARD_I2C_SCL          GPIO_NUM_14      /* [HWREF][BSP][SCH TP_SCL / ESP32_SCL][ARD] */
#define BOARD_I2C_SDA          GPIO_NUM_15      /* [HWREF][BSP][SCH TP_SDA / ESP32_SDA][ARD] */
#define BOARD_I2C_HZ           400000           /* [HWREF] "maintained BSP defaults to 400 kHz" */
#define BOARD_TOUCH_ADDR       0x5A             /* [HWREF][cst9217 driver] 7-bit */
#define BOARD_TOUCH_RST        GPIO_NUM_40      /* [HWREF][BSP][SCH TP_RESET][ARD] */
#define BOARD_TOUCH_INT        GPIO_NUM_11      /* [HWREF][BSP][SCH TP_INT][ARD] */

/* ---- QMI8658 IMU (shared I2C bus) ---------------------------------------- */
#define BOARD_IMU_ADDR         0x6B             /* [HWREF] "IMU address selected by the board schematic" */
#define BOARD_IMU_INT2         GPIO_NUM_21      /* [HWREF][SCH QMI_INT2 -> GPIO21]; RTC-capable, usable as a sleep wake source */

/* ---- AXP2101 PMIC (shared I2C bus) ---------------------------------------- */
#define BOARD_PMIC_ADDR        0x34             /* [HWREF][SCH] */

/* ---- Power ----------------------------------------------------------------
 * The panel connector J3 is fed from VCC3V3, which is AXP2101 DCDC1, the same
 * always-on rail that powers the ESP32-S3 itself [SCH]. Neither the maintained
 * BSP nor the Arduino display examples touch the AXP2101 to bring the display
 * up [BSP][ARD]. This firmware only reads battery telemetry from it and, for
 * the final power state, issues its soft power-off. AXP2101 PWROK drives the
 * ESP32-S3 CHIP_PU, and the PWR button drives AXP2101 PWRON [SCH], so a
 * PWR press is the hardware power-on path.
 */
