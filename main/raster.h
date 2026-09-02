/*
 * Analytic scanline rasteriser for eye shapes. Fixed point (Q16.16) throughout;
 * no floating point anywhere on the per-pixel or per-row path.
 *
 * A shape is a rounded rectangle (capsule when radius == min(hw, hh)) cut by
 *   - a straight, optionally slanted top eyelid line, and
 *   - a bottom eyelid that is either straight or bulges upward in the middle
 *     (the "happy" arc).
 * Each pixel row is sampled on four sub-scanlines; horizontal coverage on the
 * edge pixels is exact. Coverage (0..255) indexes a 256-entry RGB565 LUT that
 * blends the eye colour over black.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define Q16_ONE  (1 << 16)
#define Q16(x)   ((int32_t)((x) * 65536.0))   /* compile-time constants only */

typedef struct {
    bool visible;
    /* Geometry, Q16 pixels, screen coordinates */
    int32_t cx, cy;         /* centre */
    int32_t hw, hh;         /* half width / half height */
    int32_t r;              /* corner radius, <= min(hw, hh) */
    int32_t top_base;       /* y of the top lid line at x == cx */
    int32_t slant;          /* dy/dx of the top lid line; 0 = horizontal */
    int32_t slant_rcp;      /* Q16 reciprocal of slant (valid when slant != 0) */
    int32_t bot_base;       /* y of the bottom lid at the outer edges */
    int32_t curve;          /* upward bulge of the bottom lid at x == cx, >= 0 */
    int32_t curve_rcp;      /* Q16 reciprocal of curve (valid when curve > 0) */
    /* Rotation about (cx, cy): Q16 cos/sin of the face angle (clockwise on screen), rot = false when upright */
    bool rot;
    int32_t rc, rs;
    /* Float mirror of the geometry for the rotated per-row path (set by raster_shape_finalize) */
    float fc, fs, fa, fb, fr, fhw, fhh, ftop, fbot, fslant, fcurve, finv_hw2;
    /* Pixel bounding box, [x0, x1) x [y0, y1), already clipped to the screen */
    int px0, py0, px1, py1;
    const uint16_t *lut;    /* 256-entry coverage -> RGB565 (byte order as sent) */
} raster_shape_t;

/* Fill in slant_rcp/curve_rcp and the pixel bounding box from the geometry. */
void raster_shape_finalize(raster_shape_t *s, int screen_w, int screen_h);

/* Build a coverage LUT: entry i is colour scaled by i/255, byte-swapped for the panel. */
void raster_build_lut(uint16_t lut[256], uint8_t r, uint8_t g, uint8_t b);

/*
 * Clear a band to black and draw every shape that intersects it.
 * dst holds `rows` rows of `w` RGB565 pixels covering screen rect (x0, y0, w, rows).
 * Shapes must not overlap each other.
 */
void raster_band(uint16_t *dst, int x0, int y0, int w, int rows, const raster_shape_t *shapes, int nshapes);

/* Same, but without clearing the band and without touching pixels the shapes do not cover. */
void raster_shapes_over(uint16_t *dst, int x0, int y0, int w, int rows, const raster_shape_t *shapes, int nshapes);
