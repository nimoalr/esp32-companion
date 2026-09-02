#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
    uint16_t x, y;      /* raw controller coordinates of the touch-down */
} tap_event_t;

/*
 * Bring up I2C + CST9217 and start the touch task on core 0. Taps (down and
 * up within 300 ms, less than 10 px of travel) are posted to `tap_queue`.
 */
esp_err_t touch_init(QueueHandle_t tap_queue);
