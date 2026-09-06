/*
 * Minimal QMI8658 driver: accelerometer polling for "is someone handling the
 * device" while awake, and the chip's hardware wake-on-motion (WoM) on INT2
 * (GPIO21) while asleep.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

/* Reset the chip and configure the accelerometer for polling (+-8 g, 62.5 Hz, LPF on). */
esp_err_t imu_init(void);

/* Raw accelerometer sample; 1 LSB = 8 g / 32768 (about 0.244 mg). */
esp_err_t imu_read_accel(int16_t xyz[3]);

/*
 * Reset into wake-on-motion: accelerometer in low-power 21 Hz mode, INT2 idles
 * low and goes high when any axis moves more than `threshold_mg`. Normal data
 * output is unavailable until imu_init() is called again.
 */
esp_err_t imu_enter_wom(uint8_t threshold_mg);

/* Disable both sensors and power the chip down. */
esp_err_t imu_power_down(void);
