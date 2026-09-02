#include "accessories.h"

#include <math.h>
#include <string.h>

#define W 466
#define H 466
#define CX 233
#define CY 233

/* headphones geometry */
#define CUP_R        30
#define CUP_X_L      38
#define CUP_X_R      (W - 38)
#define BAND_R       208
#define BAND_T       11
#define BAND_A0      (-78)
#define BAND_A1      78
#define HEAD_SLIDE_MS 450
#define HEAD_FROM_Q16 (-160 * 65536)

/* knocked out */
#define STAR_R       6
#define STAR_ORBIT   46
#define STAR_CY      112
#define STAR_PERIOD  1300
#define X_HALF       30
#define X_THICK      14

/* z's */
#define ZZ_X         (CX + 70)
#define ZZ_Y0        150
#define ZZ_RISE      70
#define ZZ_PERIOD    2600

static uint16_t col_cup, col_cup_in, col_band, col_star, col_x, col_zz;

static void colours(void)
{
    static bool done;
    if (done) return;
    col_cup = gfx_rgb(150, 150, 160);
    col_cup_in = gfx_rgb(60, 60, 70);
    col_band = gfx_rgb(120, 120, 130);
    col_star = gfx_rgb(255, 220, 60);
    col_x = gfx_rgb(255, 140, 0);
    col_zz = gfx_rgb(160, 160, 170);
    done = true;
}

void acc_init(accessories_t *a, int left_cx, int right_cx, int cy)
{
    memset(a, 0, sizeof(*a));
    colours();
    a->eye_cx[0] = left_cx;
    a->eye_cx[1] = right_cx;
    a->eye_cy = cy;
    a->head_y = HEAD_FROM_Q16;
    a->first = true;
}

void acc_set_angle(accessories_t *a, float deg)
{
    a->angle_deg = deg;
}

/* Rotate a point given relative to the screen centre by the face angle. */
static void rot(const accessories_t *a, float x, float y, int *ox, int *oy)
{
    const float r = a->angle_deg * 0.01745329f;
    const float c = cosf(r), s = sinf(r);
    *ox = CX + (int)(x * c - y * s);
    *oy = CY + (int)(x * s + y * c);
}

void acc_set_headphones(accessories_t *a, bool on, uint32_t now_ms)
{
    if (a->head_on == on) return;
    a->head_on = on;
    a->head_t0_ms = now_ms;
}

void acc_set_knocked_out(accessories_t *a, bool on, uint32_t now_ms)
{
    if (a->ko_on == on) return;
    a->ko_on = on;
    a->ko_t0_ms = now_ms;
}

void acc_set_zz(accessories_t *a, bool on, uint32_t now_ms)
{
    if (a->zz_on == on) return;
    a->zz_on = on;
    a->zz_t0_ms = now_ms;
}

bool acc_any(const accessories_t *a)
{
    return a->head_on || a->ko_on || a->zz_on || a->head_y != HEAD_FROM_Q16;
}

static int add(acc_rect_t *out, int n, int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    if (x0 >= x1 || y0 >= y1 || n >= ACC_MAX_DIRTY) return n;
    out[n] = (acc_rect_t){ x0, y0, x1, y1 };
    return n + 1;
}

/* Headphones bounding box for a given vertical offset (px). */
static void head_bbox(int dy, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = 0;
    *x1 = W;
    *y0 = CY - BAND_R + dy - 1;
    *y1 = CY + CUP_R + dy + 2;
}

static void star_pos(uint32_t now_ms, uint32_t t0, int i, int *x, int *y)
{
    const float ph = 6.2831853f * ((float)((now_ms - t0) % STAR_PERIOD) / (float)STAR_PERIOD) + (float)i * 2.0943951f;
    *x = CX + (int)(cosf(ph) * STAR_ORBIT);
    *y = STAR_CY + (int)(sinf(ph) * STAR_ORBIT * 0.35f);
}

static int zz_y(uint32_t now_ms, uint32_t t0)
{
    return ZZ_Y0 - (int)(((now_ms - t0) % ZZ_PERIOD) * ZZ_RISE / ZZ_PERIOD);
}

int acc_update(accessories_t *a, uint32_t now_ms, acc_rect_t out[ACC_MAX_DIRTY])
{
    int n = 0;

    if (a->angle_deg != a->prev_angle_deg) {
        a->prev_angle_deg = a->angle_deg;
        if (acc_any(a)) {
            return add(out, n, 0, 0, W, H);   /* everything moves while the face turns */
        }
    }

    /* headphones slide */
    const int32_t target = a->head_on ? 0 : HEAD_FROM_Q16;
    if (a->head_y != target) {
        uint32_t el = now_ms - a->head_t0_ms;
        if (el > HEAD_SLIDE_MS) el = HEAD_SLIDE_MS;
        float t = (float)el / (float)HEAD_SLIDE_MS;
        t = t * t * (3.f - 2.f * t);
        const int32_t from = a->head_on ? HEAD_FROM_Q16 : 0;
        int32_t ny = from + (int32_t)((float)(target - from) * t);
        if (el >= HEAD_SLIDE_MS) ny = target;
        int ax0, ay0, ax1, ay1, bx0, by0, bx1, by1;
        head_bbox(a->head_y >> 16, &ax0, &ay0, &ax1, &ay1);
        head_bbox(ny >> 16, &bx0, &by0, &bx1, &by1);
        n = add(out, n, ax0 < bx0 ? ax0 : bx0, ay0 < by0 ? ay0 : by0, ax1 > bx1 ? ax1 : bx1, ay1 > by1 ? ay1 : by1);
        a->head_y = ny;
    }

    /* knocked out: stars move every frame, X eyes appear/disappear */
    if (a->ko_on || a->ko_prev[0][0]) {
        int minx = W, miny = H, maxx = 0, maxy = 0;
        for (int i = 0; i < 3; i++) {
            int x = 0, y = 0;
            if (a->ko_on) {
                star_pos(now_ms, a->ko_t0_ms, i, &x, &y);
                rot(a, (float)(x - CX), (float)(y - CY), &x, &y);
            }
            const int px = a->ko_prev[i][0], py = a->ko_prev[i][1];
            if (px) { if (px < minx) minx = px; if (px > maxx) maxx = px; if (py < miny) miny = py; if (py > maxy) maxy = py; }
            if (a->ko_on) { if (x < minx) minx = x; if (x > maxx) maxx = x; if (y < miny) miny = y; if (y > maxy) maxy = y; }
            a->ko_prev[i][0] = a->ko_on ? x : 0;
            a->ko_prev[i][1] = a->ko_on ? y : 0;
        }
        if (minx <= maxx) n = add(out, n, minx - STAR_R - 2, miny - STAR_R - 2, maxx + STAR_R + 3, maxy + STAR_R + 3);
        if (!a->ko_on || now_ms - a->ko_t0_ms < 40) {
            for (int e = 0; e < 2; e++) {
                int cx, cy;
                rot(a, (float)(a->eye_cx[e] - CX), (float)(a->eye_cy - CY), &cx, &cy);
                const int m = X_HALF + X_THICK + 12;     /* diagonal extent when rotated */
                n = add(out, n, cx - m, cy - m, cx + m + 1, cy + m + 1);
            }
        }
    }

    /* z's rise */
    if (a->zz_on || a->zz_prev_y) {
        const int y = a->zz_on ? zz_y(now_ms, a->zz_t0_ms) : 0;
        int y0 = y, y1 = y;
        if (a->zz_prev_y) { if (a->zz_prev_y < y0) y0 = a->zz_prev_y; if (a->zz_prev_y > y1) y1 = a->zz_prev_y; }
        if (!a->zz_on) y0 = y1 = a->zz_prev_y;
        /* the glyph block, rotated: cover its bounding box at both extremes */
        int ax, ay, bx, by;
        rot(a, (float)(ZZ_X - CX), (float)(y0 - 26 - CY), &ax, &ay);
        rot(a, (float)(ZZ_X + 34 - CX), (float)(y1 + 32 - CY), &bx, &by);
        const int m = 48;
        const int mx = (ax + bx) / 2, my = (ay + by) / 2;
        n = add(out, n, mx - m, my - m, mx + m, my + m);
        a->zz_prev_y = a->zz_on ? y : 0;
    }
    return n;
}

void acc_paint(const accessories_t *a, const gfx_band_t *b, uint32_t now_ms)
{
    const int ang = (int)lroundf(a->angle_deg);
    if (a->head_on || a->head_y != HEAD_FROM_Q16) {
        const int dy = a->head_y >> 16;
        if (dy > -BAND_R - CUP_R) {
            int hx, hy, lx, ly, rx, ry;
            rot(a, 0.f, (float)dy, &hx, &hy);
            rot(a, (float)(CUP_X_L - CX), (float)dy, &lx, &ly);
            rot(a, (float)(CUP_X_R - CX), (float)dy, &rx, &ry);
            gfx_ring(b, hx, hy, BAND_R, BAND_T, BAND_A0 + ang, BAND_A1 + ang, col_band);
            gfx_disc(b, lx, ly, CUP_R, col_cup);
            gfx_disc(b, lx, ly, CUP_R - 9, col_cup_in);
            gfx_disc(b, rx, ry, CUP_R, col_cup);
            gfx_disc(b, rx, ry, CUP_R - 9, col_cup_in);
        }
    }
    if (a->ko_on) {
        for (int e = 0; e < 2; e++) {
            const float ex = (float)(a->eye_cx[e] - CX), ey = (float)(a->eye_cy - CY);
            int x0, y0, x1, y1;
            rot(a, ex - X_HALF, ey - X_HALF, &x0, &y0);
            rot(a, ex + X_HALF, ey + X_HALF, &x1, &y1);
            gfx_line(b, x0, y0, x1, y1, X_THICK, col_x);
            rot(a, ex - X_HALF, ey + X_HALF, &x0, &y0);
            rot(a, ex + X_HALF, ey - X_HALF, &x1, &y1);
            gfx_line(b, x0, y0, x1, y1, X_THICK, col_x);
        }
        for (int i = 0; i < 3; i++) {
            int x, y, rxp, ryp;
            star_pos(now_ms, a->ko_t0_ms, i, &x, &y);
            rot(a, (float)(x - CX), (float)(y - CY), &rxp, &ryp);
            gfx_disc(b, rxp, ryp, STAR_R, col_star);
        }
    }
    if (a->zz_on) {
        const int y = zz_y(now_ms, a->zz_t0_ms);
        int zx, zy;
        rot(a, (float)(ZZ_X - CX), (float)(y - CY), &zx, &zy);
        gfx_text(b, &font_spleen_16x32, zx, zy, "z", col_zz, GFX_TRANSPARENT);
        gfx_text(b, &font_spleen_16x32, zx + 18, zy - 26, "Z", col_zz, GFX_TRANSPARENT);
    }
}
