#include "lock_screen.h"

void lock_screen_paint(const gfx_band_t *b)
{
    const uint16_t ink = gfx_rgb(190, 202, 211);
    gfx_fill(b, b->x0, b->y0, b->w, b->rows, 0);
    gfx_ring(b, 233, 217, 25, 7, -90, 90, ink);
    gfx_fill(b, 208, 217, 7, 12, ink);
    gfx_fill(b, 251, 217, 7, 12, ink);
    gfx_rrect(b, 197, 224, 72, 55, 11, ink);
    gfx_disc(b, 233, 245, 6, 0);
    gfx_rrect(b, 230, 245, 6, 17, 2, 0);
}
