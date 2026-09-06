#include "eye_symbols.h"
#include <assert.h>
/* Heart outline in thousandths of the half extents. Duplicate endpoint permits splitting. */
static const int16_t heart[41][2] = {
    {0, -521},
    {4, -556},
    {30, -648},
    {94, -773},
    {203, -895},
    {354, -978},
    {530, -998},
    {707, -945},
    {860, -824},
    {964, -652},
    {1000, -452},
    {964, -245},
    {860, -44},
    {707, 144},
    {530, 322},
    {354, 489},
    {203, 645},
    {94, 786},
    {30, 899},
    {4, 974},
    {0, 1000},
    {-4, 974},
    {-30, 899},
    {-94, 786},
    {-203, 645},
    {-354, 489},
    {-530, 322},
    {-707, 144},
    {-860, -44},
    {-964, -245},
    {-1000, -452},
    {-964, -652},
    {-860, -824},
    {-707, -945},
    {-530, -998},
    {-354, -978},
    {-203, -895},
    {-94, -773},
    {-30, -648},
    {-4, -556},
    {0, -521}
};
static const int16_t seven[][2] = {
    {-850,-900}, {850,-900}, {850,-570}, {110,950},
    {-420,950}, {320,-480}, {-850,-480}
};
static const int16_t diamond[][2] = {{0,-960},{830,0},{0,960},{-830,0}};
static const int16_t crack[][2] = {{0,-521},{100,-260},{-85,40},{105,310},{-50,650},{0,1000}};

static void contour(raster_shape_t *s, const int16_t (*points)[2], int n,
                    int32_t offset_x, int32_t offset_y)
{
    int32_t xy[48][2];
    assert(n <= 48);
    for (int i = 0; i < n; i++) {
        const int32_t x = (int32_t)((int64_t)s->hw*points[i][0]/1000) + offset_x;
        const int32_t y = (int32_t)((int64_t)s->hh*points[i][1]/1000) + offset_y;
        xy[i][0] = s->cx + (int32_t)(((int64_t)x*s->rc - (int64_t)y*s->rs) >> 16);
        xy[i][1] = s->cy + (int32_t)(((int64_t)x*s->rs + (int64_t)y*s->rc) >> 16);
    }
    const bool ok = raster_path_add(s, xy, n);
    assert(ok);
    (void)ok;
}

/* Clip the scrolling strip before rotating the eye. A symbol can enter at the
 * top while the preceding one exits below; the window itself stays stationary. */
static int clip_y(const int16_t (*in)[2], int n, int16_t (*out)[2], int edge, bool lower)
{
    int count = 0;
    for (int i = 0; i < n; i++) {
        const int j = (i+n-1)%n;
        const bool a = lower ? in[j][1] >= edge : in[j][1] <= edge;
        const bool b = lower ? in[i][1] >= edge : in[i][1] <= edge;
        if (a != b) {
            out[count][0] = in[j][0] + (int32_t)(in[i][0]-in[j][0])*(edge-in[j][1])/(in[i][1]-in[j][1]);
            out[count++][1] = edge;
        }
        if (b) { out[count][0] = in[i][0]; out[count++][1] = in[i][1]; }
    }
    assert(count <= 28);
    return count;
}

static void reel_shape(raster_shape_t *s, int32_t position)
{
    const int center = position/Q16_ONE;
    for (int k = center-1; k <= center+1; k++) {
        const int y = (int32_t)((int64_t)(position-k*Q16_ONE)*2250/Q16_ONE);
        if (y < -1750 || y > 1750) continue;
        int16_t a[28][2], b[28][2];
        const int kind = (k%3+3)%3;
        const int n = kind == 0 ? 7 : kind == 1 ? 20 : 4;
        for (int i = 0; i < n; i++) {
            const int16_t *p = kind == 0 ? seven[i] : kind == 1 ? heart[i*2] : diamond[i];
            a[i][0] = p[0]*82/100;
            a[i][1] = p[1]*80/100+y;
        }
        int count = clip_y(a, n, b, -920, true);
        count = clip_y(b, count, a, 920, false);
        if (count >= 3) contour(s, a, count, 0, 0);
    }
    /* Small window lips make the stationary boundary legible as a reel. */
    static const int16_t top[][2] = {{-820,-1000},{820,-1000},{820,-960},{-820,-960}};
    static const int16_t bottom[][2] = {{-820,960},{820,960},{820,1000},{-820,1000}};
    contour(s, top, 4, 0, 0);
    contour(s, bottom, 4, 0, 0);
}

void eye_symbol_shape(raster_shape_t *s, eye_symbol_t symbol, int32_t split, int32_t reel)
{
    s->path_n = 0;
    if (symbol == EYE_SYMBOL_HEART) {
        contour(s, heart, 40, 0, 0);
    } else if (symbol == EYE_SYMBOL_BROKEN) {
        int16_t half[28][2];
        int n = 0;
        for (int i = 0; i <= 20; i++) { half[n][0]=heart[i][0]; half[n++][1]=heart[i][1]; }
        for (int i = 4; i > 0; i--) { half[n][0]=crack[i][0]; half[n++][1]=crack[i][1]; }
        const int32_t gap = (int32_t)((int64_t)s->hw*split >> 16);
        contour(s, half, n, gap, gap/2);
        n = 0;
        for (int i = 20; i <= 40; i++) { half[n][0]=heart[i][0]; half[n++][1]=heart[i][1]; }
        for (int i = 1; i < 5; i++) { half[n][0]=crack[i][0]; half[n++][1]=crack[i][1]; }
        contour(s, half, n, -gap, -gap/3);
    } else if (symbol == EYE_SYMBOL_SEVEN) {
        contour(s, seven, sizeof seven/sizeof seven[0], 0, 0);
    } else if (symbol == EYE_SYMBOL_DIAMOND) {
        contour(s, diamond, 4, 0, 0);
    } else if (symbol == EYE_SYMBOL_REEL) {
        reel_shape(s, reel);
    }
}
