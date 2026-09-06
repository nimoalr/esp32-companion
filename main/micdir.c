#include "micdir.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RAMP      8             /* samples of rise allowed before the threshold crossing */
#define EDGE_WIN  48            /* samples (3 ms) after the onset in which each channel's peak is taken */
#define DEFER     16            /* an onset this close to the frame's end is timed in the next frame */
#define QUIET     0.25f         /* the 24 samples before the ramp must stay under this share of the peak */
#define CORR_WIN  32            /* samples (2 ms) of the edge that are correlated */
#define REFRACT   64            /* samples after a timed onset in which nothing else is an onset */

void micdir_reset(micdir_t *d)
{
    memset(d, 0, sizeof *d);
    d->refr = -100000;
}

/* sample of channel ch at index j; negative j reaches into the previous frame's tail */
static inline int sig(const micdir_t *d, const int16_t *pcm, int j, int ch)
{
    if (j < 0) return j < -MICDIR_HIST ? 0 : (int)d->hist[2 * (j + MICDIR_HIST) + ch];
    return (int)pcm[2 * j + ch];
}

/* |sample| of channel ch at index j; negative j reaches into the previous frame's tail */
static inline int mag(const micdir_t *d, const int16_t *pcm, int j, int ch)
{
    if (j < 0) return j < -MICDIR_HIST ? 0 : abs((int)d->hist[2 * (j + MICDIR_HIST) + ch]);
    return abs((int)pcm[2 * j + ch]);
}

bool micdir_frame(micdir_t *d, const int16_t *pcm, int frame, int peak, float *lag, float *conf)
{
    bool timed = false;
    int timed_at = -100000;
    /* the deferred tail of the previous frame counts towards this one's peak */
    for (int j = -DEFER; j < 0; j++) {
        const int l = mag(d, pcm, j, 0), r = mag(d, pcm, j, 1);
        if (l > peak) peak = l;
        if (r > peak) peak = r;
    }
    if (peak >= MICDIR_MIN_PEAK && d->primed) {
        const int thr = peak * 3 / 10;
        int k = frame;
        for (int i = -DEFER; i < frame; i++) {
            if (mag(d, pcm, i, 0) > thr || mag(d, pcm, i, 1) > thr) { k = i; break; }
        }
        if (k >= frame - DEFER) {
            /* nothing loud, or an onset at the very end: the next frame sees it whole */
        } else if (k - d->refr < REFRACT) {
            d->loud++;                                   /* still the transient timed a moment ago */
        } else {
            /* an onset rises from near silence: look before the ramp, across the frame boundary */
            int pre = 0;
            for (int j = k - MICDIR_HIST; j < k - RAMP; j++) {
                const int l = mag(d, pcm, j, 0), r = mag(d, pcm, j, 1);
                if (l > pre) pre = l;
                if (r > pre) pre = r;
            }
            d->pre = (int16_t)(pre * 100 / peak);
            if ((float)pre < QUIET * (float)peak) {
                int pk[2] = { 0, 0 };
                bool ok = true;
                for (int ch = 0; ch < 2 && ok; ch++) {
                    for (int i = k; i < k + EDGE_WIN && i < frame; i++) {
                        const int v = mag(d, pcm, i, ch);
                        if (v > pk[ch]) pk[ch] = v;
                    }
                    if (pk[ch] < MICDIR_MIN_PEAK / 4) { ok = false; break; }   /* one mic barely heard it */
                }
                /* the edge window must hold the loud event itself, not a lull before a louder tail */
                if (ok && pk[0] < peak * 3 / 5 && pk[1] < peak * 3 / 5) ok = false;
                if (ok) {
                    /*
                     * Normalised cross-correlation over the first CORR_WIN samples of the edge,
                     * for lags -3..+3, then a parabola through the peak. Short enough to exclude
                     * the room's reflections; normalising per lag removes the window-edge bias.
                     */
                    float c[7], best_v = -2.f;
                    int best = 0;
                    for (int lag = -3; lag <= 3; lag++) {
                        float acc = 0.f, ll = 0.f, rr = 0.f;
                        for (int i = k - RAMP; i < k - RAMP + CORR_WIN && i + lag < frame && i < frame; i++) {
                            const float a = (float)sig(d, pcm, i, 0), b = (float)sig(d, pcm, i + lag, 1);
                            acc += a * b; ll += a * a; rr += b * b;
                        }
                        c[lag + 3] = acc / (sqrtf(ll * rr) + 1.f);
                        if (c[lag + 3] > best_v) { best_v = c[lag + 3]; best = lag; }
                    }
                    float l = (float)best;
                    if (best > -3 && best < 3) {
                        const float ym = c[best + 2], y0 = c[best + 3], yp = c[best + 4];
                        const float den = ym - 2.f * y0 + yp;
                        if (den < 0.f) l += 0.5f * (ym - yp) / den;
                    }
                    if (l > MICDIR_MAX_LAG) l = MICDIR_MAX_LAG;
                    if (l < -MICDIR_MAX_LAG) l = -MICDIR_MAX_LAG;
                    const float lo = (float)(pk[0] < pk[1] ? pk[0] : pk[1]), hi = (float)(pk[0] < pk[1] ? pk[1] : pk[0]);
                    *lag = l;
                    *conf = lo / hi;
                    d->n++;
                    timed = true;
                    timed_at = k;
                }
            } else {
                d->loud++;
            }
        }
    }
    d->refr = timed ? timed_at - frame : d->refr - frame;
    if (d->refr < -100000) d->refr = -100000;
    memcpy(d->hist, &pcm[2 * (frame - MICDIR_HIST)], sizeof d->hist);
    d->primed = true;
    return timed;
}
