#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    TOUCH_TAP,          /* down and up within 300 ms, under 10 px of travel */
    TOUCH_LONG_PRESS,   /* held 700 ms without moving; emitted while still down */
    TOUCH_SWIPE_UP,     /* travel of 40 px or more, dominant axis, released within 1 s */
    TOUCH_SWIPE_DOWN,
    TOUCH_SWIPE_LEFT,
    TOUCH_SWIPE_RIGHT,
} touch_event_type_t;

typedef struct {
    touch_event_type_t type;
    uint16_t x, y;      /* touch-down position, screen coordinates */
    int16_t dx, dy;     /* travel at release (swipes) */
} touch_event_t;

/*
 * Bring up the CST9217 on the shared I2C bus and start the touch task on
 * core 0. Gestures are posted to `queue`.
 */
esp_err_t touch_init(QueueHandle_t queue);

/* Timestamp (ms) of the most recent sample with a finger on the panel; 0 if never. */
uint32_t touch_last_activity_ms(void);
