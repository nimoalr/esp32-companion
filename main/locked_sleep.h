#pragma once
/* Called only on the persisted-lock boot path, after settings and I2C init.
 * Unlock persists the new state and restarts into normal initialization. */
void locked_sleep_run(void) __attribute__((noreturn));
