/*
 * Band-clipped 2D primitives for the UI: everything draws into a band buffer
 * (a horizontal strip of the screen) and clips itself to it, so screens can
 * be pushed through the same DMA band path as the eyes.
 *
 * Colours are RGB565 in panel byte order (use gfx_rgb()).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t *dst;          /* w * rows pixels */
    int x0, y0;             /* screen position of dst[0] */
    int w, rows;
} gfx_band_t;

typedef struct {
    uint8_t w, h;           /* cell size */
    uint8_t bytes_per_row;
    uint8_t ascent;
    const uint8_t *bitmap;  /* 95 ASCII glyphs then extras, h * bytes_per_row each, MSB first */
    const uint16_t *extras; /* sorted codepoints after ASCII */
    uint16_t n_extras;
} gfx_font_t;

extern const gfx_font_t font_spleen_8x16;
extern const gfx_font_t font_spleen_12x24;
extern const gfx_font_t font_spleen_16x32;

#define GFX_TRANSPARENT  (-1)

uint16_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b);
/* Colour scaled by k/255 (over black). */
uint16_t gfx_scale(uint16_t color, uint8_t k);

void gfx_fill(const gfx_band_t *b, int x, int y, int w, int h, uint16_t color);
/* Draw a UTF-8 string at (x, y) = top-left of the first cell. bg < 0 leaves the background alone. */
void gfx_text(const gfx_band_t *b, const gfx_font_t *f, int x, int y, const char *utf8, uint16_t fg, int bg);
int gfx_text_width(const gfx_font_t *f, const char *utf8);
/* Anti-aliased filled disc, drawn over whatever is in the band. */
void gfx_disc(const gfx_band_t *b, int cx, int cy, int r, uint16_t color);
/*
 * Anti-aliased ring (annulus) between r_out - thick and r_out, limited to the
 * angular sector [a0, a1] in degrees clockwise from 12 o'clock. a1 - a0 >= 360
 * draws the full ring. Drawn over black (coverage scales the colour).
 */
void gfx_ring(const gfx_band_t *b, int cx, int cy, int r_out, int thick, int a0_deg, int a1_deg, uint16_t color);

/* Decode one UTF-8 codepoint; advances *s. Returns 0 at the end of the string. */
uint32_t gfx_utf8_next(const char **s);
