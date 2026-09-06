#include "voice.h"

#include <math.h>
#include <string.h>

/* base pitch of each register, Hz: the speaker is two centimetres across and carries nothing
 * below ~400 Hz, so all three sit high and no contour goes more than 4 semitones under the base
 * (the "low" moods get their weight from buzz, breath and a darker filter, not from pitch) */
static const float k_base_hz[3] = { 330.f, 440.f, 587.f };

/* vowel formants F1, F2 (Hz) and level; at these pitches the colour comes mostly from F2 */
static const float k_vowel[VOW_COUNT][3] = {
    [VOW_NONE] = { 0, 0, 0 },
    [VOW_A] = { 750, 1250, 1.0f },
    [VOW_E] = { 500, 1900, 0.9f },
    [VOW_I] = { 320, 2400, 0.8f },
    [VOW_O] = { 480, 900, 1.0f },
    [VOW_U] = { 350, 750, 0.9f },
    [VOW_M] = { 400, 1000, 0.5f },
};

#define S(...) { __VA_ARGS__ }
const voice_gesture_t k_voice_gestures[VOICE_COUNT] = {
    /*                  name        n  syllables { ms, st0, st1, st2, vowel, vowel2, onset, gap }                                             vib      trill      breath partner att rel pulses lp   level loop */
    [VOICE_HAPPY]     = { "happy",     2, S({ 160, 0, 2, 4, VOW_E, 0, ON_NONE, 20 }, { 240, 5, 8, 9, VOW_I, 0, ON_NONE, 0 }),                6, .3,   0, 0,      0,   .35, 2, 12, 60,   0, 2800, .8, false },
    [VOICE_LAUGH]     = { "laugh",     3, S({ 180, 4, 5, 3, VOW_A, 0, ON_H, 30 }, { 160, 5, 6, 4, VOW_A, 0, ON_H, 30 }, { 200, 4, 4, 1, VOW_A, 0, ON_H, 0 }), 6, .3, 0, 0, .03, .3, 2, 5, 40, 2, 2600, .8, false },
    [VOICE_SAD]       = { "sad",       1, S({ 700, 4, 2, -4, VOW_O, 0, ON_NONE, 0 }),                                                        5, .5,   0, 0,      .04, .25, 1.5, 40, 120, 0, 1800, .7, false },
    [VOICE_SURPRISED] = { "surprised", 1, S({ 320, -2, 8, 14, VOW_O, 0, ON_NONE, 0 }),                                                       0, 0,    0, 0,      0,   .3, 2, 8, 60,    0, 3200, .9, false },
    [VOICE_SCARED]    = { "scared",    1, S({ 520, 4, 12, 13, VOW_I, 0, ON_NONE, 0 }),                                                       0, 0,    22, 2,     0,   .25, 2, 8, 80,   0, 3400, .9, false },
    [VOICE_ANGRY]     = { "angry",     1, S({ 600, -3, -2, -4, VOW_U, 0, ON_NONE, 0 }),                                                      0, 0,    38, 1,     .08, .5, 1.5, 15, 80,  0, 1500, .9, false },
    [VOICE_ANNOYED]   = { "annoyed",   2, S({ 180, -1, 0, -1, VOW_U, 0, ON_NONE, 30 }, { 200, -1, -2, -4, VOW_O, 0, ON_NONE, 0 }),           0, 0,    30, 1,     .06, .4, 1.5, 20, 60,  0, 1700, .8, false },
    [VOICE_YAWN]      = { "yawn",      1, S({ 1100, 0, 5, -4, VOW_A, 0, ON_H, 0 }),                                                          4, .6,   0, 0,      .10, .2, 2, 120, 250, 0, 1600, .7, false },
    [VOICE_PURR]      = { "purr",      1, S({ 1000, -4, -3, -4, VOW_M, 0, ON_NONE, 0 }),                                                     0, 0,    26, 0,     .12, .4, 2, 150, 250, 0, 900,  .7, true },
    [VOICE_HM]        = { "hm",        1, S({ 380, 0, 1, 6, VOW_M, 0, ON_NONE, 0 }),                                                         0, 0,    0, 0,      .04, .2, 2, 30, 60,   0, 1400, .7, false },
    [VOICE_CONFUSED]  = { "confused",  2, S({ 260, 0, 2, 5, VOW_M, 0, ON_NONE, 60 }, { 300, -1, 1, 7, VOW_M, 0, ON_NONE, 0 }),               0, 0,    0, 0,      .04, .2, 2, 30, 60,   0, 1500, .7, false },
    [VOICE_DIZZY]     = { "dizzy",     1, S({ 900, 5, 1, -4, VOW_O, 0, ON_W, 0 }),                                                           7, 2.5,  0, 0,      0,   .3, 1.5, 40, 150, 0, 2200, .8, false },
    [VOICE_PROTEST]   = { "protest",   3, S({ 140, 6, 10, 8, VOW_E, 0, ON_NONE, 30 }, { 140, 5, 9, 7, VOW_E, 0, ON_NONE, 30 }, { 180, 8, 11, 12, VOW_I, 0, ON_NONE, 0 }), 0, 0, 0, 0, 0, .3, 2, 5, 30, 0, 3000, .9, false },
    [VOICE_KO]        = { "ko",        1, S({ 900, 7, 0, -5, VOW_O, 0, ON_NONE, 0 }),                                                        3, .4,   0, 0,      .03, .25, 2, 10, 300, 0, 1800, .8, false },
    [VOICE_OH]        = { "oh",        1, S({ 300, 1, 5, 3, VOW_O, 0, ON_NONE, 0 }),                                                         0, 0,    0, 0,      0,   .25, 2, 15, 80,  0, 2000, .7, false },
    [VOICE_BLIP]      = { "blip",      1, S({ 90, 10, 12, 13, VOW_NONE, 0, ON_NONE, 0 }),                                                    0, 0,    0, 0,      0,   .2, 2, 3, 25,    0, 3800, .6, false },
    [VOICE_WAKE]      = { "wake",      1, S({ 700, -3, 0, 4, VOW_M, 0, ON_NONE, 0 }),                                                        4, .4,   0, 0,      .05, .25, 2, 150, 120, 0, 1600, .6, false },
    /* words: stress pattern in the contour, vowels and diphthongs, a consonant hint at each onset */
#define WORD 5, .3, 0, 0, .02, .3, 2, 15, 90, 0, 2800, .85, false
    [VOICE_HELLO]     = { "hello",       2, S({ 170, 0, 1, 2, VOW_E, 0, ON_H, 20 }, { 340, 7, 6, 2, VOW_O, VOW_U, ON_L, 0 }), WORD },
    [VOICE_UHOH]      = { "uh-oh",       2, S({ 200, 6, 6, 5, VOW_U, 0, ON_NONE, 70 }, { 300, 1, 0, -2, VOW_O, 0, ON_B, 0 }), WORD },
    [VOICE_WOW]       = { "wow",         1, S({ 620, -2, 6, -1, VOW_A, VOW_U, ON_W, 0 }), WORD },
    [VOICE_OHNO]      = { "oh-no",       2, S({ 220, 4, 5, 4, VOW_O, VOW_U, ON_NONE, 60 }, { 380, 6, 3, -2, VOW_O, VOW_U, ON_L, 0 }), WORD },
    [VOICE_OKAY]      = { "okay",        2, S({ 190, 1, 1, 2, VOW_O, VOW_U, ON_NONE, 40 }, { 380, 6, 9, 8, VOW_E, VOW_I, ON_K, 0 }), WORD },
    [VOICE_BYEBYE]    = { "bye-bye",     2, S({ 280, 7, 7, 5, VOW_A, VOW_I, ON_B, 50 }, { 360, 4, 3, 0, VOW_A, VOW_I, ON_B, 0 }), WORD },
    [VOICE_OOPSIE]    = { "oopsie",      2, S({ 230, 5, 5, 4, VOW_U, 0, ON_NONE, 10 }, { 300, 9, 9, 7, VOW_I, 0, ON_K, 0 }), WORD },
    [VOICE_REALLY]    = { "really?",     2, S({ 260, 2, 3, 6, VOW_I, 0, ON_L, 20 }, { 320, 7, 9, 11, VOW_I, 0, ON_L, 0 }), WORD },
    [VOICE_SERIOUSLY] = { "seriously?",  4, S({ 220, 8, 8, 7, VOW_I, 0, ON_H, 20 }, { 160, 5, 5, 4, VOW_I, 0, ON_L, 20 }, { 160, 3, 3, 2, VOW_A, 0, ON_NONE, 20 }, { 320, 4, 3, 0, VOW_I, 0, ON_L, 0 }), WORD },
    [VOICE_WHATEVER]  = { "whatever",    3, S({ 160, 3, 3, 3, VOW_A, 0, ON_W, 20 }, { 300, 8, 8, 6, VOW_E, 0, ON_K, 20 }, { 340, 3, 1, -2, VOW_E, 0, ON_W, 0 }), WORD },
    [VOICE_NOWAY]     = { "no-way",      2, S({ 200, 2, 3, 2, VOW_O, VOW_U, ON_L, 40 }, { 420, 6, 9, 3, VOW_E, VOW_I, ON_W, 0 }), WORD },
    [VOICE_THANKYOU]  = { "thank-you",   2, S({ 210, 5, 6, 5, VOW_A, 0, ON_K, 30 }, { 340, 2, 1, -1, VOW_U, 0, ON_W, 0 }), WORD },
    [VOICE_HOORAY]    = { "hooray",      2, S({ 160, 0, 1, 2, VOW_U, 0, ON_H, 20 }, { 440, 6, 11, 10, VOW_E, VOW_I, ON_L, 0 }), WORD },
    [VOICE_SORRY]     = { "sorry",       2, S({ 260, 5, 4, 3, VOW_O, 0, ON_H, 30 }, { 320, 1, 0, -2, VOW_I, 0, ON_L, 0 }), WORD },
    [VOICE_PEEKABOO]  = { "peekaboo",    3, S({ 200, 8, 9, 8, VOW_I, 0, ON_B, 30 }, { 130, 4, 4, 4, VOW_A, 0, ON_K, 30 }, { 440, 6, 9, 3, VOW_U, 0, ON_B, 0 }), WORD },
    [VOICE_BINGO]     = { "bingo",       2, S({ 200, 7, 7, 6, VOW_I, 0, ON_B, 40 }, { 340, 2, 1, -1, VOW_O, VOW_U, ON_K, 0 }), WORD },
    [VOICE_WAKEYWAKEY]= { "wakey-wakey", 4, S({ 170, 6, 7, 6, VOW_E, VOW_I, ON_W, 20 }, { 180, 9, 9, 8, VOW_I, 0, ON_K, 60 }, { 170, 5, 6, 5, VOW_E, VOW_I, ON_W, 20 }, { 240, 8, 7, 5, VOW_I, 0, ON_K, 0 }), WORD },
    [VOICE_GOODNIGHT] = { "goodnight",   2, S({ 170, 2, 2, 2, VOW_U, 0, ON_K, 20 }, { 440, 6, 4, -1, VOW_A, VOW_I, ON_L, 0 }), WORD },
    [VOICE_GOODMORNING]={ "good-morning",3, S({ 160, 2, 2, 2, VOW_U, 0, ON_K, 30 }, { 250, 7, 7, 6, VOW_O, 0, ON_L, 20 }, { 320, 4, 3, 0, VOW_I, 0, ON_L, 0 }), WORD },
    [VOICE_OOHLALA]   = { "ooh-la-la",   3, S({ 320, 4, 6, 5, VOW_U, 0, ON_NONE, 40 }, { 180, 8, 8, 7, VOW_A, 0, ON_L, 30 }, { 360, 6, 4, 1, VOW_A, 0, ON_L, 0 }), WORD },
    [VOICE_AHA]       = { "aha",         2, S({ 150, 2, 2, 3, VOW_A, 0, ON_NONE, 20 }, { 380, 9, 10, 8, VOW_A, 0, ON_H, 0 }), WORD },
    [VOICE_COMEON]    = { "come-on",     2, S({ 180, 3, 3, 4, VOW_A, 0, ON_K, 40 }, { 380, 6, 9, 10, VOW_O, 0, ON_NONE, 0 }), WORD },
    [VOICE_EXCUSEME]  = { "excuse-me",   3, S({ 140, 4, 4, 4, VOW_E, 0, ON_NONE, 20 }, { 270, 9, 9, 8, VOW_U, 0, ON_K, 30 }, { 300, 5, 3, 1, VOW_I, 0, ON_L, 0 }), WORD },
    [VOICE_HOWRUDE]   = { "how-rude",    2, S({ 230, 3, 5, 4, VOW_A, VOW_U, ON_H, 40 }, { 400, 8, 7, 3, VOW_U, 0, ON_L, 0 }), WORD },
    [VOICE_YUMMY]     = { "yummy",       2, S({ 200, 6, 7, 6, VOW_A, 0, ON_W, 20 }, { 320, 3, 2, 0, VOW_I, 0, ON_L, 0 }), WORD },
    [VOICE_BRAVO]     = { "bravo",       2, S({ 210, 4, 5, 4, VOW_A, 0, ON_B, 30 }, { 380, 8, 7, 4, VOW_O, VOW_U, ON_W, 0 }), WORD },
    [VOICE_HITHERE]   = { "hi-there",    2, S({ 280, 3, 7, 6, VOW_A, VOW_I, ON_H, 40 }, { 320, 4, 2, 0, VOW_E, 0, ON_L, 0 }), WORD },
    [VOICE_OHREALLY]  = { "oh-really?",  3, S({ 200, 5, 6, 5, VOW_O, VOW_U, ON_NONE, 60 }, { 200, 2, 3, 5, VOW_I, 0, ON_L, 20 }, { 340, 7, 9, 11, VOW_I, 0, ON_L, 0 }), WORD },
#undef WORD
};
#undef S

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
    v->reg = VOICE_REG_HIGH;
}

void voice_set_register(voice_t *v, voice_register_t reg)
{
    v->reg = reg;
}

/* band-pass biquad (constant skirt gain) at f with quality q */
static void bp_coef(float c[5], float f, float q)
{
    const float w = 2.f * (float)M_PI * f / VOICE_RATE;
    const float sn = sinf(w), cs = cosf(w);
    const float alpha = sn / (2.f * q);
    const float a0 = 1.f + alpha;
    c[0] = alpha / a0;          /* b0 */
    c[1] = 0.f;                 /* b1 */
    c[2] = -alpha / a0;         /* b2 */
    c[3] = -2.f * cs / a0;      /* a1 */
    c[4] = (1.f - alpha) / a0;  /* a2 */
}

static inline float bp_run(const float c[5], float s[4], float x)
{
    const float y = c[0] * x + c[2] * s[1] - c[3] * s[2] - c[4] * s[3];
    s[1] = s[0]; s[0] = x;
    s[3] = s[2]; s[2] = y;
    return y;
}

static void begin_seg(voice_t *v)
{
    const voice_seg_t *sg = &v->g.seg[v->seg];
    v->len = (int)(sg->ms * v->detune_len * (VOICE_RATE / 1000.f));
    if (v->len < 16) v->len = 16;
    v->gap = (int)(sg->gap_ms * (VOICE_RATE / 1000.f));
    v->pos = 0;
}

static void start_common(voice_t *v, float level)
{
    v->on = true;
    v->seg = 0;
    v->elapsed = 0;
    v->detune = 1.f + 0.03f * frand(v);
    v->detune_len = 1.f + 0.1f * frand(v);
    v->total = 0;
    for (int i = 0; i < v->g.nseg; i++) v->total += (int)((v->g.seg[i].ms + v->g.seg[i].gap_ms) * v->detune_len * (VOICE_RATE / 1000.f));
    v->ph1 = v->ph2 = v->phv = v->pht = 0.f;
    v->lp = v->nz = v->nz2 = 0.f;
    memset(v->b1, 0, sizeof v->b1);
    memset(v->b2, 0, sizeof v->b2);
    v->f1 = k_vowel[v->g.seg[0].vowel][0];
    v->f2 = k_vowel[v->g.seg[0].vowel][1];
    v->coef_ctr = 0;
    v->gain = level < 0.f ? 0.f : level > 1.f ? 1.f : level;
    v->stopping = false;
    v->fade = 1.f;
    begin_seg(v);
}

void voice_start(voice_t *v, voice_id_t id, float level)
{
    if (id < 0 || id >= VOICE_COUNT) return;
    v->g = k_voice_gestures[id];
    v->word = NULL;
    start_common(v, level);
}

void voice_babble(voice_t *v, float level, float energy)
{
    if (energy < 0.f) energy = 0.f;
    if (energy > 1.f) energy = 1.f;
    static const uint8_t vowels[] = { VOW_A, VOW_E, VOW_I, VOW_O, VOW_U, VOW_M, VOW_A, VOW_O };
    static const uint8_t onsets[] = { ON_NONE, ON_NONE, ON_NONE, ON_H, ON_B, ON_L, ON_W, ON_K };
    voice_gesture_t *g = &v->g;
    memset(g, 0, sizeof *g);
    v->word = NULL;
    g->name = "babble";
    g->nseg = 1 + (int)(rnd(v) % 4u);
    const float spread = 3.f + 6.f * energy;          /* semitones */
    float st = spread * frand(v) * 0.5f;
    for (int i = 0; i < g->nseg; i++) {
        voice_seg_t *sg = &g->seg[i];
        const float len = 110.f + 120.f * (1.f - energy) + 140.f * (0.5f + 0.5f * frand(v));
        sg->ms = len;
        sg->st0 = st;
        sg->st1 = st + spread * 0.5f * frand(v);
        st = st + spread * frand(v);
        if (st < -4.f) st = -4.f + (-4.f - st);          /* reflect: the speaker carries nothing lower */
        if (sg->st1 < -4.f) sg->st1 = -4.f;
        sg->st2 = st;
        sg->vowel = vowels[rnd(v) % 8u];
        sg->onset = onsets[rnd(v) % 8u];
        sg->gap_ms = i + 1 < g->nseg ? 20.f + 50.f * (0.5f + 0.5f * frand(v)) : 0.f;
    }
    g->vib_hz = 5.f;
    g->vib_st = 0.3f;
    g->breath = 0.02f;
    g->partner = 0.3f;
    g->partner_ratio = 2.f;
    g->attack_ms = 15.f;
    g->release_ms = 80.f;
    g->lp_hz = 2600.f;
    g->level = 0.6f + 0.2f * energy;
    start_common(v, level);
}

void voice_speak(voice_t *v, const voice_word_t *w, float level)
{
    if (!w || w->nframes < 1) return;
    /* a one-syllable gesture shell carries the global voice settings; the track drives the rest */
    voice_gesture_t *g = &v->g;
    memset(g, 0, sizeof *g);
    g->name = w->name;
    g->nseg = 1;
    g->seg[0].ms = (float)w->nframes * VOICE_FRAME_MS;
    g->seg[0].vowel = VOW_A;
    g->vib_hz = 5.5f;
    g->vib_st = 0.3f;
    g->breath = 0.f;
    g->partner = 0.3f;
    g->partner_ratio = 2.f;
    g->attack_ms = 5.f;
    g->release_ms = 20.f;
    g->lp_hz = 3200.f;
    g->level = 0.9f;
    start_common(v, level);
    v->detune_len = 1.f;            /* the track's timing is the word's */
    v->len = w->nframes * (VOICE_RATE / 1000) * VOICE_FRAME_MS;
    v->word = w;
    v->wpos = 0;
    v->wenv = 0.f;
    v->f1 = w->frames[0].f1 * 8.f;
    v->f2 = w->frames[0].f2 * 16.f;
}

void voice_stop(voice_t *v)
{
    if (v->on && v->g.loop) v->stopping = true;
}

bool voice_active(const voice_t *v)
{
    return v->on;
}

/* pitch in semitones at time t (0..1) along a syllable: smooth through start, middle, end */
static float contour(const voice_seg_t *sg, float t)
{
    float a, b, f;
    if (t < 0.5f) { a = sg->st0; b = sg->st1; f = t * 2.f; }
    else { a = sg->st1; b = sg->st2; f = (t - 0.5f) * 2.f; }
    const float s = f * f * (3.f - 2.f * f);
    return a + (b - a) * s;
}

/* one sample of a spoken word: the track's frames, interpolated, drive the same tone,
 * resonators and noise as the gestures */
static int16_t speak_sample(voice_t *v)
{
    const voice_word_t *w = v->word;
    const voice_gesture_t *g = &v->g;
    const int hop = VOICE_RATE / 1000 * VOICE_FRAME_MS;
    const int f = v->pos / hop;
    const float t = (float)(v->pos - f * hop) / (float)hop;
    const voice_frame_t *a = &w->frames[f < w->nframes ? f : w->nframes - 1];
    const voice_frame_t *b = &w->frames[f + 1 < w->nframes ? f + 1 : w->nframes - 1];
    const float st = (a->st4 + (b->st4 - a->st4) * t) * 0.25f;
    const float lvl = (a->level + (b->level - a->level) * t) * (1.f / 255.f);
    const float f1 = (a->f1 + (b->f1 - a->f1) * t) * 8.f, f2 = (a->f2 + (b->f2 - a->f2) * t) * 16.f;
    const bool voiced = a->voiced;
    v->wenv += (lvl - v->wenv) * 0.02f;
    /* pitch with vibrato */
    v->phv += g->vib_hz / VOICE_RATE;
    if (v->phv >= 1.f) v->phv -= 1.f;
    const float hz = k_base_hz[v->reg] * v->detune * exp2f((st + g->vib_st * sine(v->phv)) * (1.f / 12.f));
    v->ph1 += hz / VOICE_RATE;
    if (v->ph1 >= 1.f) v->ph1 -= 1.f;
    v->ph2 += hz * g->partner_ratio / VOICE_RATE;
    if (v->ph2 >= 1.f) v->ph2 -= 1.f;
    /* formants glide toward the frame's */
    if (v->coef_ctr == 0) {
        v->f1 += (f1 - v->f1) * 0.5f;
        v->f2 += (f2 - v->f2) * 0.5f;
        bp_coef(v->c1, v->f1 < 150.f ? 150.f : v->f1, 5.f);
        bp_coef(v->c2, v->f2 < 300.f ? 300.f : v->f2, 7.f);
    }
    v->coef_ctr = (v->coef_ctr + 1) & 31;
    float s;
    if (voiced) {
        const float tone = sine(v->ph1) + 0.25f * sine(v->ph1 * 2.f) + g->partner * sine(v->ph2);
        const float rich = sine(v->ph1) + 0.5f * sine(v->ph1 * 2.f) + 0.33f * sine(v->ph1 * 3.f) +
                           0.25f * sine(v->ph1 * 4.f) + 0.2f * sine(v->ph1 * 5.f);
        const float form = bp_run(v->c1, v->b1, rich) * 1.5f + bp_run(v->c2, v->b2, rich) * 1.8f;
        s = 0.55f * tone + form;
    } else {
        /* a consonant: bright noise through the same resonators, plus a little raw hiss */
        const float nz = frand(v);
        s = (bp_run(v->c1, v->b1, nz) * 1.2f + bp_run(v->c2, v->b2, nz) * 2.5f + 0.35f * nz) * 1.2f;
    }
    const float lp_k = 1.f - expf(-2.f * (float)M_PI * g->lp_hz / VOICE_RATE);
    v->lp += (s - v->lp) * lp_k;
    float y = v->lp * v->wenv * g->level * v->gain * 0.5f;
    const float ay = y < 0.f ? -y : y;
    if (ay > 0.8f) {
        const float over = ay - 0.8f;
        const float lim = 0.8f + over / (1.f + 4.f * over);
        y = y < 0.f ? -lim : lim;
    }
    return (int16_t)(y * 32767.f);
}

int voice_render(voice_t *v, int16_t *out, int n)
{
    if (!v->on) {
        memset(out, 0, sizeof(int16_t) * (size_t)n);
        return 0;
    }
    const voice_gesture_t *g = &v->g;
    const float base = k_base_hz[v->reg] * v->detune;
    const float att = g->attack_ms * (VOICE_RATE / 1000.f), rel = g->release_ms * (VOICE_RATE / 1000.f);
    const float lp_k = 1.f - expf(-2.f * (float)M_PI * g->lp_hz / VOICE_RATE);
    const float edge = 0.006f * VOICE_RATE;          /* 6 ms syllable edges against clicks */
    int done = 0;
    for (int i = 0; i < n; i++) {
        if (!v->on) { out[i] = 0; continue; }
        const voice_seg_t *sg = &g->seg[v->seg];
        if (v->pos >= v->len) {
            /* in the gap after a syllable */
            out[i] = 0;
            done = i + 1;
            if (--v->gap <= 0) {
                if (++v->seg >= g->nseg) {
                    if (g->loop && !v->stopping) { v->seg = 0; begin_seg(v); v->pos = (int)att; }
                    else v->on = false;
                } else {
                    begin_seg(v);
                }
            }
            continue;
        }
        if (v->word) {
            out[i] = speak_sample(v);
            done = i + 1;
            v->pos++;
            if (v->pos >= v->len) v->on = false;
            continue;
        }
        const float t = (float)v->pos / (float)v->len;
        const bool first = v->seg == 0, last = v->seg == g->nseg - 1;
        /* envelope: attack on the first syllable, release on the last, short edges elsewhere */
        float env = 1.f;
        if (first && (float)v->pos < att) env = (float)v->pos / att;
        else if ((float)v->pos < edge) env = (float)v->pos / edge;
        const float left = (float)(v->len - v->pos);
        if (last && !g->loop && left < rel) env *= left / rel;
        else if (left < edge) env *= left / edge;
        if (g->pulses) {
            const float p = 0.5f - 0.5f * sine(t * (float)g->pulses);
            env *= 0.15f + 0.85f * p * p;
        }
        /* onset consonants */
        float onset_noise = 0.f, onset_st = 0.f;
        const float ms = (float)v->pos * (1000.f / VOICE_RATE);
        switch (sg->onset) {
        case ON_H: if (ms < 45.f) { onset_noise = 0.35f * (1.f - ms / 45.f); env *= 0.4f + 0.6f * ms / 45.f; } break;
        case ON_K: if (ms < 6.f) onset_noise = 0.9f; else if (ms < 30.f) env *= (ms - 6.f) / 24.f; break;
        case ON_B: if (ms < 12.f) env = 0.f; else if (ms < 22.f) env *= (ms - 12.f) / 10.f; break;
        case ON_W: if (ms < 70.f) { onset_st = -5.f * (1.f - ms / 70.f); env *= 0.3f + 0.7f * ms / 70.f; } break;
        case ON_L: if (ms < 35.f) env *= 0.35f + 0.65f * ms / 35.f; break;
        default: break;
        }
        if (v->stopping) {
            v->fade -= 1.f / (0.15f * VOICE_RATE);
            if (v->fade <= 0.f) { v->on = false; out[i] = 0; continue; }
            env *= v->fade;
        }
        /* pitch */
        float st = contour(sg, t) + onset_st;
        if (g->vib_hz > 0.f) {
            v->phv += g->vib_hz / VOICE_RATE;
            if (v->phv >= 1.f) v->phv -= 1.f;
            st += g->vib_st * sine(v->phv);
        }
        if (g->trill_hz > 0.f) {
            v->pht += g->trill_hz / VOICE_RATE;
            if (v->pht >= 1.f) v->pht -= 1.f;
            if (g->trill_st > 0.f) st += v->pht < 0.5f ? g->trill_st : 0.f;
            else env *= 0.55f + 0.45f * sine(v->pht);     /* purr: an amplitude flutter */
        }
        const float hz = base * exp2f(st * (1.f / 12.f));
        v->ph1 += hz / VOICE_RATE;
        if (v->ph1 >= 1.f) v->ph1 -= 1.f;
        v->ph2 += hz * g->partner_ratio / VOICE_RATE;
        if (v->ph2 >= 1.f) v->ph2 -= 1.f;
        /* tone: a fundamental with a little second harmonic, and the partner */
        float s = sine(v->ph1) + 0.25f * sine(v->ph1 * 2.f) + g->partner * sine(v->ph2);
        /* vowel: a harmonic-rich source through two formant resonators; a diphthong glides to
         * its second vowel over the syllable's second half */
        const float *vw = k_vowel[(sg->vowel2 && t >= 0.5f) ? sg->vowel2 : sg->vowel];
        if (vw[2] > 0.f) {
            if (v->coef_ctr == 0) {
                v->f1 += (vw[0] - v->f1) * 0.25f;      /* formants glide between syllables (every 32 samples) */
                v->f2 += (vw[1] - v->f2) * 0.25f;
                bp_coef(v->c1, v->f1 < 100.f ? 100.f : v->f1, 5.f);
                bp_coef(v->c2, v->f2 < 100.f ? 100.f : v->f2, 7.f);
            }
            v->coef_ctr = (v->coef_ctr + 1) & 31;
            const float rich = sine(v->ph1) + 0.5f * sine(v->ph1 * 2.f) + 0.33f * sine(v->ph1 * 3.f) +
                               0.25f * sine(v->ph1 * 4.f) + 0.2f * sine(v->ph1 * 5.f);
            const float form = bp_run(v->c1, v->b1, rich) * 1.5f + bp_run(v->c2, v->b2, rich) * 1.8f;
            s = 0.55f * s + vw[2] * form;
        }
        /* breath and onset noise, darkened by two poles so it sits under the tone */
        const float nz_amt = g->breath + onset_noise;
        if (nz_amt > 0.f) {
            v->nz += (frand(v) - v->nz) * 0.12f;
            v->nz2 += (v->nz - v->nz2) * 0.12f;
            s += nz_amt * 6.f * v->nz2;
            if (onset_noise > 0.f) s += onset_noise * 0.8f * frand(v);    /* a bright edge for the consonant */
        }
        v->lp += (s - v->lp) * lp_k;
        float y = v->lp * env * g->level * v->gain * 0.4f;
        /* soft knee above 0.8 so a loud vowel rounds off instead of clipping */
        const float ay = y < 0.f ? -y : y;
        if (ay > 0.8f) {
            const float over = ay - 0.8f;
            const float lim = 0.8f + over / (1.f + 4.f * over);
            y = y < 0.f ? -lim : lim;
        }
        out[i] = (int16_t)(y * 32767.f);
        done = i + 1;
        v->pos++;
        v->elapsed++;
        if (v->pos >= v->len && v->gap <= 0) {
            /* no gap: go straight to the next syllable (legato) */
            if (++v->seg >= g->nseg) {
                if (g->loop && !v->stopping) { v->seg = 0; begin_seg(v); v->pos = (int)att; }
                else v->on = false;
            } else {
                begin_seg(v);
            }
        }
    }
    return done;
}
