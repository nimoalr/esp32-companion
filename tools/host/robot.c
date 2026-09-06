/*
 * One treatment for every clip of the character's voice, words and interjections alike,
 * so they match by construction: speed/pitch up by resampling, an optional ring
 * modulation for a metallic edge, an optional resonant peak, a high-pass for the small
 * speaker, normalisation.
 *
 *   robot in.wav out.wav [speed=1.35] [ring_hz=0] [ring_depth=0.4] [peak_hz=0]
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FS 16000

static int16_t *wav_read(const char *path, int *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    uint8_t h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4)) { fprintf(stderr, "%s: not a WAV\n", path); exit(1); }
    int16_t *pcm = NULL;
    *n = 0;
    for (;;) {
        uint8_t ch[8];
        if (fread(ch, 1, 8, f) != 8) break;
        const uint32_t len = ch[4] | ch[5] << 8 | ch[6] << 16 | (uint32_t)ch[7] << 24;
        if (!memcmp(ch, "data", 4)) { pcm = malloc(len); *n = (int)(fread(pcm, 1, len, f) / 2); break; }
        fseek(f, (long)(len + (len & 1)), SEEK_CUR);
    }
    fclose(f);
    if (!pcm) { fprintf(stderr, "%s: no data\n", path); exit(1); }
    return pcm;
}

static void wav_write(const char *path, const int16_t *pcm, int n)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    const uint32_t data = (uint32_t)n * 2, rate = FS, brate = rate * 2, fmtlen = 16, riff = 36 + data;
    const uint16_t ch = 1, bits = 16, align = 2, fmt = 1;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&fmtlen, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(pcm, 2, (size_t)n, f);
    fclose(f);
}

typedef struct { float b0, b1, b2, a1, a2, x1, x2, y1, y2; } biq_t;
static inline float biq(biq_t *q, float x)
{
    const float y = q->b0 * x + q->b1 * q->x1 + q->b2 * q->x2 - q->a1 * q->y1 - q->a2 * q->y2;
    q->x2 = q->x1; q->x1 = x; q->y2 = q->y1; q->y1 = y;
    return y;
}
static void hp_init(biq_t *q, float f, float Q)
{
    const float w = 2.f * (float)M_PI * f / FS, sn = sinf(w), cs = cosf(w), al = sn / (2.f * Q), a0 = 1.f + al;
    q->b0 = (1.f + cs) / 2.f / a0; q->b1 = -(1.f + cs) / a0; q->b2 = q->b0; q->a1 = -2.f * cs / a0; q->a2 = (1.f - al) / a0;
    q->x1 = q->x2 = q->y1 = q->y2 = 0.f;
}
static void peak_init(biq_t *q, float f, float Q, float db)
{
    const float A = powf(10.f, db / 40.f), w = 2.f * (float)M_PI * f / FS, sn = sinf(w), cs = cosf(w), al = sn / (2.f * Q);
    const float a0 = 1.f + al / A;
    q->b0 = (1.f + al * A) / a0; q->b1 = -2.f * cs / a0; q->b2 = (1.f - al * A) / a0; q->a1 = -2.f * cs / a0; q->a2 = (1.f - al / A) / a0;
    q->x1 = q->x2 = q->y1 = q->y2 = 0.f;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: robot in.wav out.wav [speed] [ring_hz] [ring_depth] [peak_hz]\n"); return 1; }
    const float speed = argc > 3 ? (float)atof(argv[3]) : 1.35f;
    const float ring_hz = argc > 4 ? (float)atof(argv[4]) : 0.f;
    const float ring_depth = argc > 5 ? (float)atof(argv[5]) : 0.4f;
    const float peak_hz = argc > 6 ? (float)atof(argv[6]) : 0.f;
    int n;
    int16_t *in = wav_read(argv[1], &n);
    const int m = (int)((float)n / speed);
    float *y = malloc(sizeof(float) * (size_t)m);
    biq_t hp, pk;
    hp_init(&hp, 320.f, 0.7f);
    if (peak_hz > 0.f) peak_init(&pk, peak_hz, 2.f, 6.f);
    for (int i = 0; i < m; i++) {
        const float src = (float)i * speed;
        const int k = (int)src;
        const float f = src - (float)k;
        float x = (k + 1 < n ? in[k] + (in[k + 1] - in[k]) * f : in[n - 1]) / 32768.f;
        if (ring_hz > 0.f) x *= 1.f - ring_depth + ring_depth * sinf(2.f * (float)M_PI * ring_hz * (float)i / FS);
        x = biq(&hp, x);
        if (peak_hz > 0.f) x = biq(&pk, x);
        y[i] = x;
    }
    float peak = 1e-6f;
    for (int i = 0; i < m; i++) if (fabsf(y[i]) > peak) peak = fabsf(y[i]);
    int16_t *out = malloc(sizeof(int16_t) * (size_t)m);
    for (int i = 0; i < m; i++) out[i] = (int16_t)(y[i] / peak * 0.7f * 32767.f);
    wav_write(argv[2], out, m);
    return 0;
}
