/*
 * Analytic scanline rasteriser for eye shapes. Fixed point (Q16.16) throughout;
 * no floating point anywhere on the per-pixel or per-row path.
 *
 * A shape is a rounded rectangle with an independent, optionally elliptical
 * radius per corner (a capsule when all four equal min(hw, hh)), optionally
 * rotated, cut by
 *   - a top eyelid line that may be slanted and bent (a parabola that dips
 *     into the eye or arches away from it), and
 *   - a bottom eyelid that may be slanted and bent the same way (the "happy"
 *     arc is a positive bend).
 * Optionally the fill is shaded by a hot spot: a separable Gaussian lightness
 * falloff in screen space, sampled from two per-axis tables.
 * Each pixel row is sampled on four sub-scanlines; horizontal coverage on the
 * edge pixels is exact. Coverage (0..255) indexes a 256-entry RGB565 LUT that
 * blends the eye colour over black.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define Q16_ONE  (1 << 16)
#define Q16(x)   ((int32_t)((x) * 65536.0))   /* compile-time constants only */

enum { RAD_TL = 0, RAD_TR, RAD_BL, RAD_BR };

#define RASTER_G2L_N 272    /* 256 + the largest dither offset */
#define RASTER_DITHER_MAX 16

typedef struct {
    bool visible;
    /* Geometry, Q16 pixels, screen coordinates */
    int32_t cx, cy;         /* centre */
    int32_t hw, hh;         /* half width / half height */
    int32_t rad[4];         /* corner x radii TL, TR, BL, BR (local frame), each <= hw */
    int32_t rady[4];        /* corner y radii, each <= hh */
    int32_t rxy[4];         /* Q16 rad / rady (set by raster_shape_finalize) */
    int32_t top_base;       /* y of the top lid line at x == cx */
    int32_t slant;          /* dy/dx of the top lid line; 0 = horizontal */
    int32_t slant_rcp;      /* Q16 reciprocal of slant (valid when slant != 0) */
    int32_t bend;           /* top lid parabola at x == cx: > 0 dips down into the eye, < 0 arches up */
    int32_t bot_base;       /* y of the bottom lid at x == cx */
    int32_t bot_slant;      /* dy/dx of the bottom lid line; 0 = horizontal */
    int32_t curve;          /* bottom lid parabola at x == cx: > 0 bulges up into the eye (happy arc), < 0 sags down */
    int32_t curve_rcp;      /* Q16 reciprocal of curve (valid when curve > 0) */
    /* Rotation about (cx, cy): Q16 cos/sin of the face angle (clockwise on screen), rot = false when upright */
    bool rot;
    int32_t rc, rs;
    bool rad_equal;         /* all eight radii equal (set by raster_shape_finalize) */
    bool bot_simple;        /* bottom lid is straight or a plain upward arc: integer path (set by raster_shape_finalize) */
    /* Float mirror of the geometry for the rotated / bent-lid per-row paths (set by raster_shape_finalize) */
    float fc, fs, frad[4], frady[4], fhw, fhh, ftop, fbot, fslant, fbslant, fcurve, fbend, finv_hw2;
    float finv_rx2[4], finv_ry2[4];   /* 1 / radius^2 per corner (set by raster_shape_finalize) */
    /*
     * Hot spot: lightness level per pixel = hot_g2l[((hot_gx[x] * hot_gy[y]) >> 8) + dither(x, y)],
     * colour = lut2[level * 64 + cov / 4]. The 2x2 ordered dither hides the 8-bit steps of the falloff.
     */
    bool hot;
    const uint8_t *hot_gx;  /* indexed by screen x */
    const uint8_t *hot_gy;  /* indexed by screen y */
    const uint8_t *hot_g2l; /* RASTER_G2L_N entries -> 0..31 */
    const uint16_t *lut2;   /* 32 x 64 entries */
    /* Pixel bounding box, [x0, x1) x [y0, y1), already clipped to the screen */
    int px0, py0, px1, py1;
    const uint16_t *lut;    /* 256-entry coverage -> RGB565 (byte order as sent) */
} raster_shape_t;

/* Fill in the derived fields (reciprocals, float mirror, flags) and the pixel bounding box from the geometry. */
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
