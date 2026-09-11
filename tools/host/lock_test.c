/* Exercise the exact firmware unlock policy, including bounce and clock wrap. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "lock_policy.h"
#include "lock_screen.h"

static bool press(lock_policy_t *p, uint32_t t)
{
    assert(!lock_policy_update(p, t, true, false));
    bool unlock = lock_policy_update(p, t + 40, true, false);
    assert(!lock_policy_update(p, t + 80, false, false));
    assert(!lock_policy_update(p, t + 120, false, false));
    return unlock;
}

int main(void)
{
    lock_policy_t p;
    lock_policy_init(&p, 0, false);
    assert(!p.bright);
    for (unsigned i = 0; i < 4; i++) assert(!press(&p, 100 + i * 700));
    assert(press(&p, 2900));
    assert(p.bright);
    assert(!lock_policy_update(&p, 7939, false, false));
    assert(p.bright);
    assert(!lock_policy_update(&p, 7940, false, false));
    assert(!p.bright);

    /* Window expiry keeps only recent presses; it does not reset the whole run. */
    lock_policy_init(&p, 0, false);
    assert(!press(&p, 100));
    for (unsigned i = 0; i < 4; i++) assert(!press(&p, 5000 + i * 200));
    assert(press(&p, 5800));
    lock_policy_init(&p, 0, false);
    for (unsigned i = 0; i < 4; i++) assert(!press(&p, 100 + i * 1000));
    assert(press(&p, 5100)); /* Exactly five seconds from first to fifth. */
    lock_policy_init(&p, 0, false);
    for (unsigned i = 0; i < 4; i++) assert(!press(&p, 100 + i * 1000));
    assert(!press(&p, 5101));

    /* Holding BOOT, including at entry, cannot manufacture repeated presses. */
    lock_policy_init(&p, 0, true);
    for (unsigned t = 0; t < 10000; t += 20)
        assert(!lock_policy_update(&p, t, true, false));
    assert(p.count == 0);
    assert(!lock_policy_update(&p, 10000, false, false));
    assert(!lock_policy_update(&p, 10040, false, false));
    assert(!press(&p, 10100));
    assert(p.count == 1);

    lock_policy_init(&p, 0, false);
    for (unsigned t = 0; t < 1000; t += 10)
        assert(!lock_policy_update(&p, t, (t / 10) % 2, false));
    assert(p.count == 0); /* Bouncing contact. */
    for (unsigned t = 1000; t < 2000; t += 20)
        assert(!lock_policy_update(&p, t, true, false));
    assert(p.count == 1);
    for (unsigned t = 2000; t < 7000; t += 20)
        assert(!lock_policy_update(&p, t, true, false));
    assert(p.count == 0); /* Held press aged out, never repeats. */

    lock_policy_init(&p, 0, false);
    for (unsigned t = 0; t < 10000; t += 200)
        assert(!lock_policy_update(&p, t, false, true));
    assert(p.count == 0 && p.bright); /* PWR only illuminates. */
    assert(!lock_policy_update(&p, 14800, false, false));
    assert(!p.bright);

    uint32_t start = UINT32_MAX - 1000;
    lock_policy_init(&p, start, false);
    for (unsigned i = 0; i < 4; i++) assert(!press(&p, start + 100 + i * 600));
    assert(press(&p, start + 2500));
    assert(!lock_policy_update(&p, start + 7540, false, false));
    assert(!p.bright);

    /* Export the same band-painted icon used on the device for visual review. */
    FILE *f = fopen("out/locked_sleep.ppm", "wb");
    assert(f);
    fprintf(f, "P6\n466 466\n255\n");
    static uint16_t pixels[466 * 16];
    for (int y = 0; y < 466; y += 16) {
        int rows = 466 - y < 16 ? 466 - y : 16;
        gfx_band_t b = {pixels, 0, y, 466, rows};
        lock_screen_paint(&b);
        for (int i = 0; i < 466 * rows; ++i) {
            uint16_t c = (pixels[i] >> 8) | (pixels[i] << 8);
            uint8_t rgb[] = {(c >> 11) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3};
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    puts("PASS: five-press unlock, rolling deadline, bounce, held buttons, PWR isolation, brightness and timer wrap");
    return 0;
}
