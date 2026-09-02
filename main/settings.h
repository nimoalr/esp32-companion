/* Runtime settings and calibration, persisted in NVS. Defaults come from Kconfig. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "imu_cal.h"

typedef struct {
    uint8_t brightness_active;   /* percent, ACTIVE state */
    uint8_t brightness_aod;      /* percent, DROWSY (always-on) state */
    imu_cal_t cal;               /* cal.valid == false until the wizard has run */
} settings_t;

extern settings_t g_settings;

/* Initialise NVS and load everything that is stored; missing keys keep their defaults. */
esp_err_t settings_init(void);
esp_err_t settings_save(void);
