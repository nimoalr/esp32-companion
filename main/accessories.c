#include "accessories.h"

#include <math.h>
#include <string.h>

#define W 466
#define H 466
#define CX 233
#define CY 233

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

/* charge gauge on the rim: same arc as the setup UI's battery screen, thinner */
#define CHG_R_OUT    232
#define CHG_THICK    4
#define CHG_A0       (-150)
#define CHG_A1       150
#define CHG_BAND     16         /* dirty rects are cut on the display's band grid */
#define CHG_SWEEP    ((float)(CHG_A1 - CHG_A0))
#define CHG_TAU_MS   160.f      /* ease of the sweeps */
#define CHG_MAX_DPS  900.f      /* ...and their speed cap */

static uint16_t col_star, col_x, col_zz, col_chg, col_track;

static int add(acc_rect_t *out, int n, int x0, int y0, int x1, int y1);

static void colours(void)
{
    static bool done;
    if (done) return;
    col_star = gfx_rgb(255, 220, 60);
    col_x = gfx_rgb(255, 140, 0);
    col_zz = gfx_rgb(160, 160, 170);
    col_chg = gfx_rgb(70, 210, 100);
    col_track = gfx_rgb(26, 26, 30);
    done = true;
}

void acc_init(accessories_t *a, int left_cx, int right_cx, int cy)
{
    memset(a, 0, sizeof(*a));
    colours();
    a->eye_cx[0] = left_cx;
    a->eye_cx[1] = right_cx;
    a->eye_cy = cy;
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

/* Props that turn with the face (the rim gauge does not). */
static bool any_prop(const accessories_t *a)
{
    return a->ko_on || a->zz_on;
}

bool acc_any(const accessories_t *a)
{
    return any_prop(a) || a->chg_track > 0.f || a->chg_arc > 0.f;
}

void acc_set_charge(accessories_t *a, bool on, int pct, bool charging)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    a->chg_on = on;
    a->chg_pct = pct;
    a->chg_charging = charging;
}

void acc_redraw(accessories_t *a)
{
    if (a->chg_track > 0.f || a->chg_arc > 0.f) a->chg_dirty = true;
}

static bool gauge_visible(const accessories_t *a)
{
    return a->chg_track > 0.f || a->chg_arc > 0.f;
}

/* Move `cur` toward `tgt` with an ease-out, capped in speed. */
static float approach(float cur, float tgt, float dt_ms, float tau_ms, float max_per_s)
{
    float d = tgt - cur;
    float step = d * (dt_ms / (dt_ms + tau_ms));
    const float cap = max_per_s * dt_ms / 1000.f;
    if (step > cap) step = cap;
    if (step < -cap) step = -cap;
    if (fabsf(d) < 0.3f || fabsf(step) >= fabsf(d)) return tgt;
    return cur + step;
}

/* Bounding box of the ring segment between two sweep angles (degrees from CHG_A0), padded. */
static int add_arc_rect(acc_rect_t *out, int n, float d0, float d1)
{
    if (d0 > d1) { const float t = d0; d0 = d1; d1 = t; }
    if (d1 - d0 < 0.01f) return n;
    int x0 = W, y0 = H, x1 = 0, y1 = 0;
    for (float d = d0; ; d += 3.f) {
        if (d > d1) d = d1;
        const float rad = ((float)CHG_A0 + d) * 0.01745329f;
        const float sx = sinf(rad), cy = -cosf(rad);
        for (int k = 0; k < 2; k++) {
            const float r = k ? (float)(CHG_R_OUT - CHG_THICK) : (float)CHG_R_OUT;
            const int px = CX + (int)lrintf(sx * r), py = CY + (int)lrintf(cy * r);
            if (px < x0) x0 = px;
            if (px > x1) x1 = px;
            if (py < y0) y0 = py;
            if (py > y1) y1 = py;
        }
        if (d >= d1) break;
    }
    return add(out, n, x0 - 4, y0 - 4, x1 + 5, y1 + 5);
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

/* The rim ring's footprint in each display band: one wide rect at the top and bottom, two slivers elsewhere. */
static int add_ring_rects(acc_rect_t *out, int n)
{
    const int ro = CHG_R_OUT, ri = CHG_R_OUT - CHG_THICK;
    for (int y0 = 0; y0 < H; y0 += CHG_BAND) {
        const int y1 = y0 + CHG_BAND < H ? y0 + CHG_BAND : H;
        /* nearest and farthest row distances from the centre within the band */
        int dmin, dmax;
        if (y1 - 1 < CY) { dmin = CY - (y1 - 1); dmax = CY - y0; }
        else if (y0 > CY) { dmin = y0 - CY; dmax = y1 - 1 - CY; }
        else { dmin = 0; dmax = (CY - y0 > y1 - 1 - CY) ? CY - y0 : y1 - 1 - CY; }
        if (dmin >= ro) continue;
        const int so = (int)sqrtf((float)(ro * ro - dmin * dmin)) + 2;
        if (dmax >= ri) {
            n = add(out, n, CX - so, y0, CX + so + 1, y1);
        } else {
            const int si = (int)sqrtf((float)(ri * ri - dmax * dmax)) - 2;
            n = add(out, n, CX - so, y0, CX - si, y1);
            n = add(out, n, CX + si, y0, CX + so + 1, y1);
        }
    }
    return n;
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

    /* rim gauge: the track sweeps in first, then the arc; on unplug the arc retracts, then the track */
    {
        const float dt = a->chg_ms ? (float)(now_ms - a->chg_ms) : 16.f;
        a->chg_ms = now_ms;
        const float arc_tgt = CHG_SWEEP * (float)a->chg_pct / 100.f;
        float track_to = a->chg_track, arc_to = a->chg_arc;
        if (a->chg_on) {
            track_to = CHG_SWEEP;
            if (a->chg_track >= CHG_SWEEP * 0.4f) arc_to = arc_tgt;
        } else {
            arc_to = 0.f;
            if (a->chg_arc <= CHG_SWEEP * 0.2f) track_to = 0.f;
        }
        a->chg_track = approach(a->chg_track, track_to, dt, CHG_TAU_MS, CHG_MAX_DPS);
        a->chg_arc = approach(a->chg_arc, arc_to, dt, CHG_TAU_MS, CHG_MAX_DPS);
        if (a->chg_dirty) {
            a->chg_dirty = false;
            n = add_ring_rects(out, n);
        } else {
            if (a->chg_track != a->chg_prev_track) n = add_arc_rect(out, n, a->chg_prev_track, a->chg_track);
            if (a->chg_arc != a->chg_prev_arc) n = add_arc_rect(out, n, a->chg_prev_arc, a->chg_arc);
        }
        a->chg_prev_track = a->chg_track;
        a->chg_prev_arc = a->chg_arc;
    }
    if (a->angle_deg != a->prev_angle_deg) {
        const float from = a->prev_angle_deg;
        a->prev_angle_deg = a->angle_deg;
        (void)from;
        if (a->ko_on || a->zz_on || a->ko_prev[0][0] || a->zz_prev_y) {
            return add(out, 0, 0, 0, W, H);   /* rare states: everything moves while the face turns */
        }
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

/* True when the whole band lies inside the disc of radius r about the centre (nothing of a rim ring can be in it). */
static bool inside_disc(const gfx_band_t *b, int r)
{
    const int xs[2] = { b->x0, b->x0 + b->w - 1 }, ys[2] = { b->y0, b->y0 + b->rows - 1 };
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            const int dx = xs[i] - CX, dy = ys[j] - CY;
            if (dx * dx + dy * dy >= r * r) return false;
        }
    }
    return true;
}

void acc_paint(const accessories_t *a, const gfx_band_t *b, uint32_t now_ms)
{
    colours();
    if (gauge_visible(a) && !inside_disc(b, CHG_R_OUT - CHG_THICK)) {
        if (a->chg_track > 0.f) gfx_ring(b, CX, CY, CHG_R_OUT, CHG_THICK, CHG_A0, CHG_A0 + (int)lrintf(a->chg_track), col_track);
        if (a->chg_arc > 0.f) gfx_ring(b, CX, CY, CHG_R_OUT, CHG_THICK, CHG_A0, CHG_A0 + (int)lrintf(a->chg_arc), col_chg);
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
