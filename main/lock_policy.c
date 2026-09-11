#include "lock_policy.h"
#include <string.h>

void lock_policy_init(lock_policy_t *p, uint32_t now, bool boot_down)
{
    *p = (lock_policy_t){ .raw_since = now, .raw_down = boot_down,
                          .stable_down = boot_down };
}

bool lock_policy_update(lock_policy_t *p, uint32_t now, bool boot_down, bool power_press)
{
    if (p->bright && now - p->bright_since >= LOCK_BRIGHT_MS) p->bright = false;
    while (p->count && now - p->pressed_at[0] > LOCK_WINDOW_MS) {
        --p->count;
        memmove(p->pressed_at, p->pressed_at + 1, p->count * sizeof(uint32_t));
    }
    if (boot_down != p->raw_down) {
        p->raw_down = boot_down;
        p->raw_since = now;
    }
    bool press = false;
    if (p->raw_down != p->stable_down && now - p->raw_since >= LOCK_DEBOUNCE_MS) {
        p->stable_down = p->raw_down;
        press = p->stable_down;
    }
    if (press || power_press) {
        p->bright = true;
        p->bright_since = now;
    }
    if (press) {
        p->pressed_at[p->count++] = now;
        if (p->count == 5) {
            p->count = 0; /* Also safe if persistence fails and the caller stays locked. */
            return true;
        }
    }
    return false;
}
