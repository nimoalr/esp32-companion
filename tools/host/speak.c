/*
 * Speech -> word tracks for the procedural voice. Reads the dry text-to-speech clips
 * that words.sh produced (16 kHz mono WAV), extracts per 10 ms frame the pitch movement
 * (semitones around the clip's median, expanded), the loudness, the first two formants
 * (LPC) and a voiced flag, writes them as C tables into main/words_gen.c and renders
 * every word with main/voice.c for listening, plus a medley and a chirp/word mix.
 *
 *   speak <clip dir> [expand=2.0]
 */
#include <dirent.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "voice.h"

#define FS 16000
#define HOP (FS / 100)
#define WIN (FS * 25 / 1000)
#define LPC_ORDER 12
#define MAX_WORDS 96
#define MAX_FRAMES 400

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

/* first two formants of a 25 ms window by LPC: the peaks of 1/|A(w)|^2 */
static void formants(const float *x, int n, float *f1, float *f2)
{
    float w[WIN], r[LPC_ORDER + 1], a[LPC_ORDER + 1], tmp[LPC_ORDER + 1];
    for (int i = 0; i < WIN; i++) {
        const float pre = i < n ? x[i] - 0.95f * (i > 0 ? x[i - 1] : 0.f) : 0.f;
        w[i] = pre * (0.54f - 0.46f * cosf(2.f * (float)M_PI * i / (WIN - 1)));
    }
    for (int k = 0; k <= LPC_ORDER; k++) { r[k] = 0.f; for (int i = k; i < WIN; i++) r[k] += w[i] * w[i - k]; }
    if (r[0] < 1e-9f) return;
    /* Levinson-Durbin */
    memset(a, 0, sizeof a);
    a[0] = 1.f;
    float err = r[0];
    for (int i = 1; i <= LPC_ORDER; i++) {
        float acc = r[i];
        for (int j = 1; j < i; j++) acc += a[j] * r[i - j];
        const float k = -acc / err;
        memcpy(tmp, a, sizeof a);
        for (int j = 1; j < i; j++) a[j] = tmp[j] + k * tmp[i - j];
        a[i] = k;
        err *= 1.f - k * k;
        if (err <= 0.f) return;
    }
    /* spectrum on a 512-point grid, peaks */
    float p[256];
    for (int k = 0; k < 256; k++) {
        const float wv = (float)M_PI * k / 256.f;
        float re = 1.f, im = 0.f;
        for (int j = 1; j <= LPC_ORDER; j++) { re += a[j] * cosf(wv * j); im -= a[j] * sinf(wv * j); }
        p[k] = 1.f / (re * re + im * im + 1e-9f);
    }
    float pk[8];
    int npk = 0;
    for (int k = 1; k < 255 && npk < 8; k++) if (p[k] > p[k - 1] && p[k] >= p[k + 1]) pk[npk++] = k * (FS / 2) / 256.f;
    float c1 = 0.f, c2 = 0.f;
    for (int i = 0; i < npk; i++) if (pk[i] >= 250.f && pk[i] <= 1100.f) { c1 = pk[i]; break; }
    if (c1 == 0.f) return;
    for (int i = 0; i < npk; i++) if (pk[i] >= c1 + 250.f && pk[i] <= 3300.f) { c2 = pk[i]; break; }
    if (c2 == 0.f) return;
    *f1 = c1;
    *f2 = c2;
}

static float pitch(const float *x, int n)
{
    const int win = FS * 30 / 1000, lmin = FS / 400, lmax = FS / 80;
    float e0 = 0.f;
    for (int i = 0; i < win && i < n; i++) e0 += x[i] * x[i];
    float best = 0.f;
    int bl = 0;
    for (int l = lmin; l <= lmax; l++) {
        float acc = 0.f, el = 0.f;
        for (int i = 0; i < win && i + l < n; i++) { acc += x[i] * x[i + l]; el += x[i + l] * x[i + l]; }
        const float r = acc / (sqrtf(e0 * el) + 1e-9f);
        if (r > best) { best = r; bl = l; }
    }
    return (best > 0.55f && e0 > 1e-4f * win) ? (float)FS / (float)bl : 0.f;
}

typedef struct { char name[48], slug[48]; int n; voice_frame_t fr[MAX_FRAMES]; } word_t;
static word_t g_words[MAX_WORDS];
static int g_nwords;

static int analyse(const char *path, const char *name, const char *slug, float expand, word_t *w)
{
    int n;
    int16_t *pcm = wav_read(path, &n);
    float *x = malloc(sizeof(float) * (size_t)n);
    for (int i = 0; i < n; i++) x[i] = pcm[i] / 32768.f;
    const int frames = n / HOP;
    if (frames > MAX_FRAMES) { fprintf(stderr, "%s: too long\n", name); exit(1); }
    float hz[MAX_FRAMES], rms[MAX_FRAMES], f1[MAX_FRAMES], f2[MAX_FRAMES];
    float peak_rms = 1e-6f, cf1 = 500.f, cf2 = 1500.f;
    for (int f = 0; f < frames; f++) {
        const int s = f * HOP;
        float e = 0.f;
        for (int i = 0; i < HOP; i++) e += x[s + i] * x[s + i];
        rms[f] = sqrtf(e / HOP);
        if (rms[f] > peak_rms) peak_rms = rms[f];
        hz[f] = s + FS * 30 / 1000 <= n ? pitch(x + s, n - s) : 0.f;
        if (s + WIN <= n) formants(x + s, n - s, &cf1, &cf2);
        f1[f] = cf1;
        f2[f] = cf2;
    }
    /* median voiced pitch */
    float vl[MAX_FRAMES];
    int nv = 0;
    for (int f = 0; f < frames; f++) if (hz[f] > 0.f) vl[nv++] = hz[f];
    for (int a = 1; a < nv; a++) { const float t = vl[a]; int b = a; while (b > 0 && vl[b - 1] > t) { vl[b] = vl[b - 1]; b--; } vl[b] = t; }
    const float median = nv ? vl[nv / 2] : 200.f;
    /* frames */
    int first = -1, last = -1;
    float st_hold = 0.f;
    for (int f = 0; f < frames; f++) {
        const float ratio = rms[f] / peak_rms;
        const float level = ratio < 0.03f ? 0.f : powf(ratio, 0.7f);
        if (level > 0.f) { if (first < 0) first = f; last = f; }
        if (hz[f] > 0.f) {
            float st = expand * 12.f * log2f(hz[f] / median);
            if (st > 12.f) st = 12.f;
            if (st < -4.f) st = -4.f;
            st_hold += (st - st_hold) * 0.6f;
        }
        voice_frame_t *o = &w->fr[f];
        o->st4 = (int8_t)lrintf(st_hold * 4.f);
        o->level = (uint8_t)(level * 255.f);
        o->f1 = (uint8_t)(f1[f] / 8.f > 255.f ? 255 : f1[f] / 8.f);
        o->f2 = (uint8_t)(f2[f] / 16.f > 255.f ? 255 : f2[f] / 16.f);
        o->voiced = hz[f] > 0.f ? 1 : 0;
    }
    /* unvoiced holes of one frame inside voiced stretches are pitch glitches */
    for (int f = 1; f + 1 < frames; f++) if (!w->fr[f].voiced && w->fr[f - 1].voiced && w->fr[f + 1].voiced) w->fr[f].voiced = 1;
    /* trim silence, keep a frame either side */
    if (first < 0) { w->n = 0; return 0; }
    const int a = first > 0 ? first - 1 : 0, b = last + 2 < frames ? last + 2 : frames;
    memmove(w->fr, w->fr + a, sizeof(voice_frame_t) * (size_t)(b - a));
    w->n = b - a;
    snprintf(w->name, sizeof w->name, "%s", name);
    snprintf(w->slug, sizeof w->slug, "%s", slug);
    printf("%-18s %3d frames, median %.0f Hz\n", name, w->n, median);
    free(x);
    free(pcm);
    return 1;
}

static int16_t g_buf[FS * 4], g_med[MAX_WORDS * FS * 2];

static int render(voice_t *v, const voice_word_t *w, int16_t *buf)
{
    voice_speak(v, w, 1.f);
    int n = 0;
    while (voice_active(v) && n < FS * 4 - 160) n += voice_render(v, buf + n, 160);
    return n;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: speak <clip dir> [expand]\n"); return 1; }
    const char *dir = argv[1];
    const float expand = argc > 2 ? (float)atof(argv[2]) : 2.f;
    char path[512];
    snprintf(path, sizeof path, "%s/list.txt", dir);
    FILE *lf = fopen(path, "r");
    if (!lf) { perror(path); return 1; }
    char line[128];
    while (fgets(line, sizeof line, lf) && g_nwords < MAX_WORDS) {
        int idx;
        char name[96];
        if (sscanf(line, "%d %[^\n]", &idx, name) != 2) continue;
        char slug[96];
        int k = 0;
        for (const char *c = name; *c && k < 90; c++) {
            const bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9');
            if (ok) slug[k++] = *c;
            else if (k && slug[k - 1] != '_') slug[k++] = '_';
        }
        while (k && slug[k - 1] == '_') k--;
        slug[k] = 0;
        snprintf(path, sizeof path, "%s/%02d_%s_dry.wav", dir, idx, slug);
        if (analyse(path, name, slug, expand, &g_words[g_nwords])) g_nwords++;
    }
    fclose(lf);

    /* C tables */
    FILE *cf = fopen("../../main/words_gen.c", "w");
    if (!cf) { perror("main/words_gen.c"); return 1; }
    fprintf(cf, "/* Generated by tools/host/speak.c from text-to-speech clips: do not edit. */\n#include \"words_gen.h\"\n\n");
    size_t bytes = 0;
    for (int i = 0; i < g_nwords; i++) {
        fprintf(cf, "static const voice_frame_t f_%s[%d] = {", g_words[i].slug, g_words[i].n);
        for (int f = 0; f < g_words[i].n; f++) {
            const voice_frame_t *o = &g_words[i].fr[f];
            fprintf(cf, "%s{%d,%u,%u,%u,%u}", f % 8 ? "," : ",\n    ", o->st4, o->level, o->f1, o->f2, o->voiced);
        }
        fprintf(cf, "\n};\n");
        bytes += sizeof(voice_frame_t) * (size_t)g_words[i].n;
    }
    fprintf(cf, "\nconst voice_word_t k_words[%d] = {\n", g_nwords);
    for (int i = 0; i < g_nwords; i++) fprintf(cf, "    { \"%s\", %d, f_%s },\n", g_words[i].name, g_words[i].n, g_words[i].slug);
    fprintf(cf, "};\nconst int k_words_n = %d;\n", g_nwords);
    fclose(cf);
    FILE *hf = fopen("../../main/words_gen.h", "w");
    fprintf(hf, "/* Generated by tools/host/speak.c: the spoken word tracks. */\n#pragma once\n#include \"voice.h\"\n\n");
    fprintf(hf, "enum {\n");
    for (int i = 0; i < g_nwords; i++) {
        char up[96];
        int k = 0;
        for (const char *c = g_words[i].slug; *c; c++) up[k++] = (*c >= 'a' && *c <= 'z') ? *c - 32 : *c;
        up[k] = 0;
        fprintf(hf, "    WORD_%s,\n", up);
    }
    fprintf(hf, "    WORD_COUNT\n};\nextern const voice_word_t k_words[WORD_COUNT];\nextern const int k_words_n;\n");
    fclose(hf);
    printf("%d words, %zu bytes of tracks -> main/words_gen.c\n", g_nwords, bytes);

    /* renders */
    mkdir("out/spoken", 0755);
    voice_t v;
    voice_init(&v, 777u);
    voice_set_register(&v, VOICE_REG_HIGH);
    int m = 0;
    for (int i = 0; i < g_nwords; i++) {
        const voice_word_t w = { g_words[i].name, g_words[i].n, g_words[i].fr };
        const int n = render(&v, &w, g_buf);
        snprintf(path, sizeof path, "out/spoken/%02d_%s.wav", i, g_words[i].slug);
        wav_write(path, g_buf, n);
        memcpy(g_med + m, g_buf, sizeof(int16_t) * (size_t)n);
        m += n;
        memset(g_med + m, 0, sizeof(int16_t) * (FS * 4 / 10));
        m += FS * 4 / 10;
    }
    wav_write("../../docs/voice/words_spoken.wav", g_med, m);
    /* chirp / word pairs */
    static const int pairs[][2] = { { VOICE_HAPPY, 0 }, { VOICE_OH, 1 }, { VOICE_SURPRISED, 2 }, { VOICE_HM, 7 }, { VOICE_ANNOYED, 9 },
                                    { VOICE_ANGRY, 28 }, { VOICE_YAWN, 17 }, { VOICE_WAKE, 16 }, { VOICE_CONFUSED, 8 }, { VOICE_LAUGH, 46 } };
    m = 0;
    for (int i = 0; i < 10; i++) {
        if (pairs[i][1] >= g_nwords) continue;
        voice_start(&v, (voice_id_t)pairs[i][0], 1.f);
        int n = 0;
        while (voice_active(&v) && n < FS * 4 - 160) n += voice_render(&v, g_buf + n, 160);
        memcpy(g_med + m, g_buf, sizeof(int16_t) * (size_t)n);
        m += n + FS * 3 / 10;
        const voice_word_t w = { g_words[pairs[i][1]].name, g_words[pairs[i][1]].n, g_words[pairs[i][1]].fr };
        n = render(&v, &w, g_buf);
        memcpy(g_med + m, g_buf, sizeof(int16_t) * (size_t)n);
        m += n;
        memset(g_med + m, 0, sizeof(int16_t) * FS);
        m += FS;
    }
    wav_write("../../docs/voice/mix_spoken.wav", g_med, m);
    printf("written docs/voice/words_spoken.wav and mix_spoken.wav\n");
    return 0;
}
