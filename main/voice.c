#include "voice.h"

#include <math.h>
#include <string.h>

/* base pitch of each register, Hz */
/* the speaker is two centimetres across: nothing below ~300 Hz carries, so the registers sit high */
static const float k_base_hz[3] = { 330.f, 440.f, 587.f };

#define P(...) { __VA_ARGS__ }
const voice_gesture_t k_voice_gestures[VOICE_COUNT] = {
    /*                  name        ms   pitch contour (t, semitones)                                       vib hz,st  trill hz,st  breath partner,ratio  att,rel  pulses  lp    level  loop */
    [VOICE_HAPPY]     = { "happy",     420, 3, P({ 0.f, 0.f }, { 0.45f, 4.f }, { 1.f, 9.f }),                 6.f, 0.3f, 0, 0,      0.00f, 0.35f, 2.f,  12, 60,   0, 2800, 0.8f, false },
    [VOICE_LAUGH]     = { "laugh",     620, 3, P({ 0.f, 2.f }, { 0.5f, 5.f }, { 1.f, 3.f }),                  6.f, 0.4f, 0, 0,      0.03f, 0.30f, 2.f,  5,  40,   5, 2600, 0.8f, false },
    [VOICE_SAD]       = { "sad",       700, 3, P({ 0.f, 3.f }, { 0.3f, 1.f }, { 1.f, -7.f }),                 5.f, 0.5f, 0, 0,      0.04f, 0.25f, 1.5f, 40, 120,  0, 1800, 0.7f, false },
    [VOICE_SURPRISED] = { "surprised", 320, 3, P({ 0.f, -2.f }, { 0.6f, 8.f }, { 1.f, 14.f }),                0,   0,    0, 0,      0.00f, 0.30f, 2.f,  8,  60,   0, 3200, 0.9f, false },
    [VOICE_SCARED]    = { "scared",    520, 3, P({ 0.f, 4.f }, { 0.4f, 12.f }, { 1.f, 13.f }),                0,   0,    22.f, 2.f, 0.00f, 0.25f, 2.f,  8,  80,   0, 3400, 0.9f, false },
    [VOICE_ANGRY]     = { "angry",     600, 3, P({ 0.f, -10.f }, { 0.5f, -9.f }, { 1.f, -12.f }),             0,   0,    38.f, 1.f, 0.08f, 0.50f, 1.5f, 15, 80,   0, 1500, 0.9f, false },
    [VOICE_ANNOYED]   = { "annoyed",   380, 3, P({ 0.f, -6.f }, { 0.5f, -5.f }, { 1.f, -9.f }),               0,   0,    30.f, 1.f, 0.06f, 0.40f, 1.5f, 20, 60,   2, 1700, 0.8f, false },
    [VOICE_YAWN]      = { "yawn",      1100, 4, P({ 0.f, -2.f }, { 0.3f, 3.f }, { 0.6f, 1.f }, { 1.f, -9.f }), 4.f, 0.6f, 0, 0,     0.10f, 0.20f, 2.f,  120, 250, 0, 1600, 0.7f, false },
    [VOICE_PURR]      = { "purr",      1000, 2, P({ 0.f, -14.f }, { 1.f, -13.f }),                            0,   0,    26.f, 0.f, 0.12f, 0.40f, 2.f,  150, 250, 0, 900,  0.7f, true },
    [VOICE_HM]        = { "hm",        380, 3, P({ 0.f, -1.f }, { 0.6f, 0.f }, { 1.f, 6.f }),                 0,   0,    0, 0,      0.04f, 0.20f, 2.f,  30, 60,   0, 1400, 0.7f, false },
    [VOICE_CONFUSED]  = { "confused",  640, 4, P({ 0.f, -1.f }, { 0.4f, 5.f }, { 0.55f, -2.f }, { 1.f, 7.f }), 0, 0,    0, 0,      0.04f, 0.20f, 2.f,  30, 60,   2, 1500, 0.7f, false },
    [VOICE_DIZZY]     = { "dizzy",     900, 3, P({ 0.f, 4.f }, { 0.5f, 0.f }, { 1.f, -6.f }),                 7.f, 2.5f, 0, 0,      0.00f, 0.30f, 1.5f, 40, 150,  0, 2200, 0.8f, false },
    [VOICE_PROTEST]   = { "protest",   560, 4, P({ 0.f, 6.f }, { 0.3f, 10.f }, { 0.6f, 5.f }, { 1.f, 11.f }), 0,   0,    0, 0,      0.00f, 0.30f, 2.f,  5,  30,   4, 3000, 0.9f, false },
    [VOICE_KO]        = { "ko",        900, 3, P({ 0.f, 6.f }, { 0.4f, -2.f }, { 1.f, -16.f }),               3.f, 0.4f, 0, 0,      0.03f, 0.25f, 2.f,  10, 300,  0, 1800, 0.8f, false },
    [VOICE_OH]        = { "oh",        300, 3, P({ 0.f, 1.f }, { 0.5f, 5.f }, { 1.f, 3.f }),                  0,   0,    0, 0,      0.00f, 0.25f, 2.f,  15, 80,   0, 2000, 0.7f, false },
    [VOICE_BLIP]      = { "blip",      90,  2, P({ 0.f, 10.f }, { 1.f, 13.f }),                               0,   0,    0, 0,      0.00f, 0.20f, 2.f,  3,  25,   0, 3800, 0.6f, false },
    [VOICE_WAKE]      = { "wake",      700, 3, P({ 0.f, -5.f }, { 0.7f, -1.f }, { 1.f, 4.f }),                4.f, 0.4f, 0, 0,      0.05f, 0.25f, 2.f,  150, 120, 0, 1600, 0.6f, false },
};
#undef P

/* 256-entry sine table, linear interpolation: cheap enough for two oscillators at 16 kHz */
static float s_sin[257];
static bool s_tables;

static void tables(void)
{
    if (s_tables) return;
    for (int i = 0; i <= 256; i++) s_sin[i] = sinf((float)i * (2.f * (float)M_PI / 256.f));
    s_tables = true;
}

static inline float sine(float ph)     /* ph 0..1 */
{
    const float x = ph * 256.f;
    const int i = (int)x;
    const float f = x - (float)i;
    return s_sin[i & 255] + (s_sin[(i & 255) + 1] - s_sin[i & 255]) * f;
}

static inline uint32_t rnd(voice_t *v)
{
    v->rng = v->rng * 1664525u + 1013904223u;
    return v->rng;
}

static inline float frand(voice_t *v)     /* -1..1 */
{
    return (float)(int32_t)rnd(v) * (1.f / 2147483648.f);
}

void voice_init(voice_t *v, uint32_t seed)
{
    tables();
    memset(v, 0, sizeof *v);
    v->rng = seed ? seed : 1;
    v->reg = VOICE_REG_MID;
}

void voice_set_register(voice_t *v, voice_register_t reg)
{
    v->reg = reg;
}

void voice_start(voice_t *v, voice_id_t id, float level)
{
    if (id < 0 || id >= VOICE_COUNT) return;
    v->g = &k_voice_gestures[id];
    v->pos = 0;
    v->len = (int)(v->g->dur_ms * (1.f + 0.1f * frand(v)) * (VOICE_RATE / 1000.f));
    v->detune = 1.f + 0.03f * frand(v);
    v->ph1 = v->ph2 = v->phv = v->pht = 0.f;
    v->lp = v->nz = v->nz2 = 0.f;
    v->gain = level < 0.f ? 0.f : level > 1.f ? 1.f : level;
    v->stopping = false;
    v->fade = 1.f;
}

void voice_stop(voice_t *v)
{
    if (v->g && v->g->loop) v->stopping = true;
}

bool voice_active(const voice_t *v)
{
    return v->g != NULL;
}

/* pitch in semitones at time t (0..1) along the contour */
static float contour(const voice_gesture_t *g, float t)
{
    if (t <= g->pts[0].t) return g->pts[0].st;
    for (int i = 1; i < g->npts; i++) {
        if (t <= g->pts[i].t) {
            const float a = g->pts[i - 1].t, b = g->pts[i].t;
            const float f = b > a ? (t - a) / (b - a) : 1.f;
            /* smooth step between breakpoints: no corners in the pitch */
            const float s = f * f * (3.f - 2.f * f);
            return g->pts[i - 1].st + (g->pts[i].st - g->pts[i - 1].st) * s;
        }
    }
    return g->pts[g->npts - 1].st;
}

int voice_render(voice_t *v, int16_t *out, int n)
{
    if (!v->g) {
        memset(out, 0, sizeof(int16_t) * (size_t)n);
        return 0;
    }
    const voice_gesture_t *g = v->g;
    const float base = k_base_hz[v->reg] * v->detune;
    const float att = g->attack_ms * (VOICE_RATE / 1000.f), rel = g->release_ms * (VOICE_RATE / 1000.f);
    const float lp_k = 1.f - expf(-2.f * (float)M_PI * g->lp_hz / VOICE_RATE);
    int done = 0;
    for (int i = 0; i < n; i++) {
        if (!v->g) { out[i] = 0; continue; }
        const float t = (float)v->pos / (float)v->len;
        /* envelope */
        float env = 1.f;
        if ((float)v->pos < att) env = (float)v->pos / att;
        const float left = (float)(v->len - v->pos);
        if (!g->loop && left < rel) env *= left / rel;
        if (g->pulses) {
            /* raised-cosine pulses over the utterance, never fully closed */
            const float p = 0.5f - 0.5f * sine(t * (float)g->pulses);
            env *= 0.15f + 0.85f * p * p;
        }
        if (v->stopping) {
            v->fade -= 1.f / (0.15f * VOICE_RATE);
            if (v->fade <= 0.f) { v->g = NULL; out[i] = 0; continue; }
            env *= v->fade;
        }
        /* pitch */
        float st = contour(g, t);
        if (g->vib_hz > 0.f) {
            v->phv += g->vib_hz / VOICE_RATE;
            if (v->phv >= 1.f) v->phv -= 1.f;
            st += g->vib_st * sine(v->phv);
        }
        if (g->trill_hz > 0.f) {
            v->pht += g->trill_hz / VOICE_RATE;
            if (v->pht >= 1.f) v->pht -= 1.f;
            if (g->trill_st > 0.f) st += v->pht < 0.5f ? g->trill_st : 0.f;
            else env *= 0.55f + 0.45f * sine(v->pht);     /* purr: the trill is an amplitude flutter */
        }
        const float hz = base * exp2f(st * (1.f / 12.f));
        v->ph1 += hz / VOICE_RATE;
        if (v->ph1 >= 1.f) v->ph1 -= 1.f;
        v->ph2 += hz * g->partner_ratio / VOICE_RATE;
        if (v->ph2 >= 1.f) v->ph2 -= 1.f;
        /* oscillators: a fundamental with a little second harmonic for colour, and the partner */
        float s = sine(v->ph1) + 0.25f * sine(v->ph1 * 2.f) + g->partner * sine(v->ph2);
        if (g->noise > 0.f) {
            /* breath: noise darkened by two poles so it sits under the tone instead of hissing over it */
            v->nz += (frand(v) - v->nz) * 0.12f;
            v->nz2 += (v->nz - v->nz2) * 0.12f;
            s += g->noise * 6.f * v->nz2;
        }
        /* filter and output */
        v->lp += (s - v->lp) * lp_k;
        float y = v->lp * env * g->level * v->gain * 0.45f;
        if (y > 1.f) y = 1.f;
        if (y < -1.f) y = -1.f;
        out[i] = (int16_t)(y * 32767.f);
        done = i + 1;
        v->pos++;
        if (v->pos >= v->len) {
            if (g->loop && !v->stopping) v->pos = (int)(att);      /* loop past the attack */
            else v->g = NULL;
        }
    }
    return done;
}
