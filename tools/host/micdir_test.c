/* Synthetic claps with known fractional delays through micdir: onsets inside a frame and
 * straddling a frame boundary, on silence and over loud background music, both polarities. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "micdir.h"

#define FRAME 256
#define FS 16000.f

static int g_noise;     /* 0: tone burst, 1: band-limited noise burst */
#define NOISE_N 128
static float g_nz[NOISE_N];

/* the burst's waveform at time t_ms after its onset: a tone, or a fixed noise burst (sinc-interpolated) */
static float clap(float t_ms, float amp)
{
    if (t_ms < 0.f) return 0.f;
    const float rise = 1.f - expf(-t_ms / 0.12f);
    const float env = amp * rise * expf(-t_ms / 2.5f);
    if (!g_noise) return env * sinf(2.f * (float)M_PI * 2500.f * t_ms / 1000.f + 0.4f);
    /* band-limited (0.9 x Nyquist) noise sampled at a fractional position */
    const float pos = t_ms * FS / 1000.f;
    float v = 0.f;
    for (int n = 0; n < NOISE_N; n++) {
        const float x = (float)M_PI * (pos - (float)n) * 0.9f;
        v += g_nz[n] * (fabsf(x) < 1e-4f ? 1.f : sinf(x) / x);
    }
    return env * v;
}

static int run(float delay, float bg, float onset_ms, int frames, float amp_l, float amp_r, int verbose)
{
    micdir_t d;
    micdir_reset(&d);
    static int16_t pcm[FRAME * 2];
    unsigned seed = 1;
    int got = 0;
    micdir_result_t r = { 0 };
    for (int f = 0; f < frames; f++) {
        int peak = 0;
        for (int i = 0; i < FRAME; i++) {
            const float t_ms = (float)(f * FRAME + i) * 1000.f / FS;
            const float noise = 30.f * ((float)(rand_r(&seed) % 2001) / 1000.f - 1.f);
            const float music = bg * sinf(2.f * (float)M_PI * 180.f * t_ms / 1000.f) + 0.4f * bg * sinf(2.f * (float)M_PI * 1300.f * t_ms / 1000.f);
            /* positive delay: the sound reaches L first, R later */
            float l = clap(t_ms - onset_ms, amp_l) + music + noise;
            float r = clap(t_ms - onset_ms - delay * 1000.f / FS, amp_r) + music + noise;
            if (l > 32767.f) l = 32767.f; if (l < -32768.f) l = -32768.f;
            if (r > 32767.f) r = 32767.f; if (r < -32768.f) r = -32768.f;
            pcm[2 * i] = (int16_t)l;
            pcm[2 * i + 1] = (int16_t)r;
            if (abs((int)l) > peak) peak = abs((int)l);
            if (abs((int)r) > peak) peak = abs((int)r);
        }
        if (micdir_frame(&d, pcm, FRAME, peak, &r)) { got++; if (verbose > 1) printf("  frame %d peak %d lag %+.2f corr %.2f refr %d\n", f, peak, r.lag, r.corr, (int)d.refr); }
    }
    const int ok = got == 1 && fabsf(r.lag - delay) < 0.15f && r.corr > 0.6f;
    if (verbose || !ok)
        printf("%s delay %+.2f bg %5.0f onset %6.2f ms amp %5.0f/%5.0f -> %d timed, lag %+.2f conf %.2f, loud %u pre %d%%\n",
               ok ? "ok  " : "FAIL", delay, bg, onset_ms, amp_l, amp_r, got, r.lag, r.balance, d.loud, d.pre);
    return ok;
}

int main(int argc, char **argv)
{
    const int verbose = argc > 1 ? atoi(argv[1]) : 0;
    int fails = 0;
    unsigned seed = 7;
    for (int n = 0; n < NOISE_N; n++) g_nz[n] = (float)(rand_r(&seed) % 2001) / 1000.f - 1.f;
    for (g_noise = 0; g_noise < 2; g_noise++) {
    printf("-- %s bursts\n", g_noise ? "noise" : "tone");
    const float delays[] = { -1.9f, -1.2f, -0.5f, 0.f, 0.4f, 1.f, 1.9f };
    const float bgs[] = { 0.f, 400.f, 1200.f };
    const float onsets[] = { 21.f, 31.7f, 31.95f, 32.05f, 56.f };   /* frame 0 is a warm-up: nothing is timed before the tail history exists */
    for (int a = 0; a < 7; a++)
        for (int b = 0; b < 3; b++)
            for (int c = 0; c < 5; c++) {
                fails += !run(delays[a], bgs[b], onsets[c], 6, 12000.f, 9000.f, verbose);
                fails += !run(delays[a], bgs[b], onsets[c], 6, 40000.f, 45000.f, verbose);   /* clipping */
            }
    }
    printf("%d failures\n", fails);
    return fails != 0;
}
