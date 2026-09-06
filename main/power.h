/*
 * Power state machine.
 *
 *   ACTIVE  full brightness, 60 fps, CPU at max, no light sleep.
 *   DROWSY  dimmed always-on display, drowsy face, reduced frame rate, DFS and
 *           automatic light sleep between frames. Entered after
 *           EYES_ACTIVE_TIMEOUT_S without touch or motion; any touch or motion
 *           returns to ACTIVE. Held for at least EYES_DROWSY_MIN_S.
 *   SLEEP   panel off, IMU in wake-on-motion, ESP32 in light sleep until the
 *           IMU or the touch controller fires (-> ACTIVE) or
 *           EYES_SLEEP_TO_DEEP_S elapse (-> DEEP).
 *   DEEP    AXP2101 power off (PWR button restarts the board) or, by Kconfig,
 *           ESP32 deep sleep with IMU wake.
 *
 * The render task drives this: it calls power_update() once per frame and
 * performs the display-side effects of each transition itself, so the panel
 * is only ever touched from one task.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "pmic.h"

typedef enum {
    POWER_ACTIVE = 0,
    POWER_DROWSY,
    POWER_SLEEP,
    POWER_DEEP,
} power_state_t;

typedef enum {
    POWER_WAKE_MOTION,
    POWER_WAKE_TOUCH,
    POWER_WAKE_TIMEOUT,
    POWER_WAKE_OTHER,
} power_wake_t;

esp_err_t power_init(void);

/*
 * Run the timers and the motion detector. `touch_ms` is the last touch
 * activity timestamp (0 = none). Returns the current state; compare with the
 * previous return value to detect transitions.
 */
power_state_t power_update(uint32_t now_ms, uint32_t touch_ms);
power_state_t power_state(void);
const char *power_state_name(power_state_t s);

/* True while USB power is present (and the USB-keeps-awake option is on): no light sleep, no SLEEP state. */
bool power_on_usb(void);

/* In DROWSY: allow (true) or forbid (false) automatic light sleep. No-op in other states. */
void power_allow_light_sleep(bool allow);

/* SLEEP: block in light sleep for at most `max_ms`. Restores the IMU to polling mode on return. */
/* PWR button press since the last call: 0 none, 1 short, 2 long (2 s). A press counts as activity. */
int power_take_key(void);

power_wake_t power_light_sleep(uint32_t max_ms);

/* Back to ACTIVE after a wake (resets the inactivity timer). */
void power_wake_to_active(uint32_t now_ms);

/* DEEP: does not return. */
void power_enter_deep(void) __attribute__((noreturn));

/* Latest raw accelerometer sample and its timestamp (ms); returns false if the IMU is unavailable. */
bool power_last_accel(int16_t raw[3], uint32_t *sample_ms);

/* Battery telemetry, refreshed every few seconds by power_update(). */
void power_battery(pmic_battery_t *out);
bool power_motion_recent(uint32_t now_ms, uint32_t window_ms);
