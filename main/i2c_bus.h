#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

/* The board has one I2C bus (GPIO14 SCL, GPIO15 SDA) shared by touch, IMU and PMIC. */
esp_err_t i2c_bus_init(void);
i2c_master_bus_handle_t i2c_bus_get(void);

/* Convenience: add a 7-bit device at BOARD_I2C_HZ. */
esp_err_t i2c_bus_add_device(uint8_t addr, i2c_master_dev_handle_t *dev);
