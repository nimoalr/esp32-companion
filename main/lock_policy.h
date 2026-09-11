#pragma once
#include <stdbool.h>
#include <stdint.h>

#define LOCK_WINDOW_MS 5000u
#define LOCK_DEBOUNCE_MS 40u
#define LOCK_BRIGHT_MS 5000u

typedef struct {
    uint32_t raw_since, pressed_at[5], bright_since;
    unsigned count;
    bool raw_down, stable_down, bright;
} lock_policy_t;

/* A button already held on entry must be released before it counts. */
void lock_policy_init(lock_policy_t *p, uint32_t now, bool boot_down);
/* True only on the fifth debounced BOOT press in a rolling five-second window.
 * PWR events brighten the icon but never contribute to the unlock sequence. */
bool lock_policy_update(lock_policy_t *p, uint32_t now, bool boot_down, bool power_press);
