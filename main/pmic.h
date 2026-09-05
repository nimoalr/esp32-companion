/*
 * Minimal AXP2101 access: battery telemetry and system power-off.
 * Rails are left exactly as the PMIC brings them up; nothing here changes a
 * voltage or enables an output (see docs/hardware.md, "Power").
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool present;       /* battery connected */
    bool charging;
    bool vbus;          /* USB power present */
    uint16_t mv;        /* battery voltage, 0 if not present */
    int8_t percent;     /* fuel gauge, -1 if not present */
} pmic_battery_t;

esp_err_t pmic_init(void);
esp_err_t pmic_read_battery(pmic_battery_t *out);
/* USB power present? One register read; for quick plug/unplug detection. */
esp_err_t pmic_read_vbus(bool *vbus);

/* Cut every rail (except the RTC LDO). Only the PWR button or USB brings the board back. */
esp_err_t pmic_power_off(void);
