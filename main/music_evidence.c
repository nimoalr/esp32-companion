#include "music_evidence.h"
#include <math.h>
#include <string.h>

void music_evidence_reset(music_evidence_t *s)
{
    memset(s, 0, sizeof *s);
}

void music_evidence_update(music_evidence_t *s, float bass, float high,
                           float raw_loud, bool muted)
{
    if (muted) { music_evidence_reset(s); return; }
    s->level += .02f * (raw_loud-s->level);
    const float level[2] = {bass, high};
    /* Average pairs before decimation: 32 ms envelopes retain the rhythm,
     * suppress frame-to-frame noise and need only 2 KiB of history. */
    for (int b = 0; b < 2; b++) s->pair[b] += level[b];
    if (++s->frames % 2 == 0) {
        for (int b = 0; b < 2; b++) {
            const float v = .5f * s->pair[b];
            s->pair[b] = 0.f;
            s->mean[b] += .05f * (v - s->mean[b]);
            const float modulation = (v - s->mean[b]) / (s->mean[b] + .001f);
            s->history[b][s->cursor] = raw_loud < 45.f ? 0.f :
                fmaxf(-1.f, fminf(3.f, modulation));
        }
        s->cursor = (s->cursor + 1) & 255;
    }
    if (s->frames < 448) return; /* seven seconds of context before admission */
    if (!s->next_lag) {
        s->anchor = s->cursor;
        s->next_lag = 10;
        s->best = s->support = 0.f;
        s->best_lag = 0;
    }
    /* Four lags/frame, 1280 pairs maximum. All lags share the same 5.12 s
     * window. At most five new samples arrive during a sweep; 160+47+5<256,
     * so the frozen window cannot be overwritten. Lags cover 40–187.5 bpm
     * (including double-beat periods); this is evidence, not a beat clock. */
    for (int work = 0; work < 4 && s->next_lag <= 47; work++, s->next_lag++) {
        float c[2];
        for (int b = 0; b < 2; b++) {
            float xy = 0.f, xx = 0.f, yy = 0.f;
            for (unsigned k = 0; k < 160; k++) {
                const float x = s->history[b][(s->anchor - 1 - k) & 255];
                const float y = s->history[b][(s->anchor - 1 - k - s->next_lag) & 255];
                xy += x*y; xx += x*x; yy += y*y;
            }
            c[b] = xx > 1.f && yy > 1.f ? xy / sqrtf(xx*yy) : 0.f;
        }
        const float strongest = fmaxf(c[0], c[1]);
        if (strongest > s->best) {
            s->best = strongest;
            s->support = fminf(c[0], c[1]);
            s->best_lag = s->next_lag;
        }
    }
    if (s->next_lag <= 47) return;
    s->next_lag = 0;
    s->confidence = s->best;
    s->bpm = s->best_lag ? 1875.f / s->best_lag : 0.f;
    const float change = fabsf((float)s->best_lag - s->previous_lag);
    const bool stable = s->previous_lag && change <= .08f*s->previous_lag;
    /* Strong repeating modulation, corroboration in a different band and
     * persistence. A sustained tone or loudness alone is insufficient. */
    const bool supported = s->level > 80.f && s->best >= .65f &&
                           s->support >= .12f && stable;
    s->previous_lag = s->best_lag;
    s->evidence = fmaxf(0.f, fminf(4.f, s->evidence + (supported ? .16f : -.16f)));
}
