#include "audio.h"
#include "micdir.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es7210_adc.h"
#include "es8311_codec.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "board.h"
#include "i2c_bus.h"

static const char *TAG = "audio";

#define SAMPLE_RATE     16000
#define FRAME           256                 /* samples per channel per analysis frame (16 ms) */
#define BINS            (FRAME / 2)         /* 62.5 Hz per bin */
#define BIN_BASS_LO     1                   /* 62 Hz */
#define BIN_BASS_HI     4                   /* 312 Hz */
#define BIN_MID_HI      32                  /* 2 kHz */
#define BEAT_MIN_GAP_MS 250                 /* 240 bpm ceiling before the tempo lock stretches it */
#define KICK_LP_HZ      80.f                /* the kick's body; psytrance/techno basslines sit above this */
#define PRESENCE_FLOOR_LSB 30.f             /* raw RMS below this is room noise (measured ~18 in a quiet room) */
#define PRESENCE_FULL_LSB  150.f            /* ...and above this it is unmistakably sound */

static i2s_chan_handle_t s_rx, s_tx;

static esp_codec_dev_handle_t s_dev;
static esp_codec_dev_handle_t s_spk;                /* ES8311 playback device, same data interface */
static const audio_codec_ctrl_if_t *s_spk_ctrl_if;  /* kept across start/stop like the ES7210's */
static const audio_codec_if_t *s_spk_codec_if;
static bool s_spk_opened;
static int s_volume = 70;
static volatile bool s_muted;
static int16_t s_stereo[2 * 320];
static const audio_codec_ctrl_if_t *s_ctrl_if;   /* created once, kept across start/stop */
static bool s_opened;                             /* esp_codec_dev_open succeeded */
static const audio_codec_if_t *s_codec_if;
static const audio_codec_data_if_t *s_data_if;
static TaskHandle_t s_task;
static volatile bool s_run;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static audio_features_t s_feat;

/* analysis state */
static float s_win[FRAME];
static float s_tw_cos[FRAME / 2], s_tw_sin[FRAME / 2];
static float s_re[FRAME], s_im[FRAME];
static float s_max_bass = 1.f, s_max_mid = 1.f, s_max_high = 1.f, s_max_loud = 1.f;
static float s_bass_mean, s_bass_prev;
/* kick detector: 2nd-order low-pass at KICK_LP_HZ on the mono signal, energy per frame */
static float s_lp_b0, s_lp_b1, s_lp_b2, s_lp_a1, s_lp_a2;
static float s_lp_x1, s_lp_x2, s_lp_y1, s_lp_y2;
static float s_kick_mean, s_kick_prev, s_max_kick = 1.f;
static float s_bass_ratio;
static float s_dir, s_dir_conf, s_dir_lag;
static int s_gain_db = CONFIG_EYES_AUDIO_GAIN_DB;
static float s_dir_off = 0.f, s_dir_gain = 0.5f;   /* raw lag -> -1..+1; the wizard sets these */
static float s_dir_db_off = 0.f, s_dir_db_gain = 0.f; /* level difference (dB) -> -1..+1; 0 = unused */
#define DIR_MAX_LAG 3                 /* cross-correlation lags for sustained sound */
static micdir_t s_micdir;
static float s_dir_corr;
static int s_dir_peak;
static float s_dir_level_db;
static float s_presence;              /* 0..1: is there real sound, from the raw level (quiet room ~18 LSB) */
static float s_band_max[16], s_bands[16];
/* speech: the mid band's envelope pulses at syllable rate (3-8 Hz) */
static float s_sp_fast, s_sp_slow, s_sp_mod;
static int s_sp_on, s_sp_off;
static bool s_speech;
static uint16_t s_dir_seen_n;
static uint32_t s_transient_ms;
static float s_gap_ms;                /* current tempo estimate as a beat interval, 0 = none */
static uint32_t s_last_beat_ms;
static uint32_t s_beat_gaps[8];
static int s_gap_idx, s_gap_n;
static float s_balance;

static void tables_init(void)
{
    /* Butterworth low-pass biquad (bilinear transform) */
    const float w0 = 6.2831853f * KICK_LP_HZ / (float)SAMPLE_RATE;
    const float cw = cosf(w0), sw = sinf(w0), alpha = sw / (2.f * 0.7071f);
    const float a0 = 1.f + alpha;
    s_lp_b0 = (1.f - cw) * 0.5f / a0;
    s_lp_b1 = (1.f - cw) / a0;
    s_lp_b2 = s_lp_b0;
    s_lp_a1 = -2.f * cw / a0;
    s_lp_a2 = (1.f - alpha) / a0;
    for (int i = 0; i < FRAME; i++) {
        s_win[i] = 0.5f - 0.5f * cosf(6.2831853f * (float)i / (float)(FRAME - 1));
    }
    for (int i = 0; i < FRAME / 2; i++) {
        s_tw_cos[i] = cosf(-6.2831853f * (float)i / (float)FRAME);
        s_tw_sin[i] = sinf(-6.2831853f * (float)i / (float)FRAME);
    }
}

/* In-place radix-2 FFT of s_re/s_im, FRAME points. */
static void fft(void)
{
    /* bit reversal */
    for (int i = 1, j = 0; i < FRAME; i++) {
        int bit = FRAME >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = s_re[i]; s_re[i] = s_re[j]; s_re[j] = t;
            t = s_im[i]; s_im[i] = s_im[j]; s_im[j] = t;
        }
    }
    for (int len = 2; len <= FRAME; len <<= 1) {
        const int half = len >> 1;
        const int step = FRAME / len;
        for (int i = 0; i < FRAME; i += len) {
            for (int k = 0; k < half; k++) {
                const float wr = s_tw_cos[k * step], wi = s_tw_sin[k * step];
                const int a = i + k, b = a + half;
                const float xr = s_re[b] * wr - s_im[b] * wi;
                const float xi = s_re[b] * wi + s_im[b] * wr;
                s_re[b] = s_re[a] - xr; s_im[b] = s_im[a] - xi;
                s_re[a] += xr;          s_im[a] += xi;
            }
        }
    }
}

static inline float agc(float level, float *running_max)
{
    /* slow decay so a quiet passage does not blow the gain up instantly */
    *running_max *= 0.9985f;
    if (level > *running_max) *running_max = level;
    if (*running_max < 1e-3f) *running_max = 1e-3f;
    float n = level / *running_max;
    return n > 1.f ? 1.f : n;
}

static void analyse(const int16_t *pcm, uint32_t now_ms)
{
    float l_e = 0.f, r_e = 0.f, k_e = 0.f;
    int peak = 0;
    for (int i = 0; i < FRAME; i++) {
        /* sub-bass energy for the kick detector */
        const float x = (float)((int)pcm[2 * i] + (int)pcm[2 * i + 1]) * (0.5f / 32768.f);
        const float y = s_lp_b0 * x + s_lp_b1 * s_lp_x1 + s_lp_b2 * s_lp_x2 - s_lp_a1 * s_lp_y1 - s_lp_a2 * s_lp_y2;
        s_lp_x2 = s_lp_x1; s_lp_x1 = x; s_lp_y2 = s_lp_y1; s_lp_y1 = y;
        k_e += y * y;
        const int al = abs((int)pcm[2 * i]), ar = abs((int)pcm[2 * i + 1]);
        if (al > peak) peak = al;
        if (ar > peak) peak = ar;
        const float l = (float)pcm[2 * i] * (1.f / 32768.f);
        const float r = (float)pcm[2 * i + 1] * (1.f / 32768.f);
        l_e += l * l;
        r_e += r * r;
        s_re[i] = 0.5f * (l + r) * s_win[i];
        s_im[i] = 0.f;
    }
    fft();

    float bass = 0.f, mid = 0.f, high = 0.f;
    for (int k = BIN_BASS_LO; k < BINS; k++) {
        const float p = s_re[k] * s_re[k] + s_im[k] * s_im[k];
        if (k <= BIN_BASS_HI) bass += p;
        else if (k <= BIN_MID_HI) mid += p;
        else high += p;
    }
    bass = sqrtf(bass);
    mid = sqrtf(mid);
    high = sqrtf(high);
    /* sixteen log-spaced bands for the spectrum eyes, each against its own slow-decaying maximum */
    {
        static const uint8_t k_edge[17] = { 1, 2, 3, 4, 5, 6, 7, 9, 12, 16, 21, 28, 38, 51, 68, 91, 128 };
        for (int b = 0; b < 16; b++) {
            float pw = 0.f;
            for (int k = k_edge[b]; k < k_edge[b + 1]; k++) pw += s_re[k] * s_re[k] + s_im[k] * s_im[k];
            const float lv = sqrtf(pw);
            s_band_max[b] *= 0.998f;
            if (lv > s_band_max[b]) s_band_max[b] = lv;
            if (s_band_max[b] < 1e-4f) s_band_max[b] = 1e-4f;
            float n = lv / s_band_max[b];
            s_bands[b] = sqrtf(n) * s_presence;       /* a gentle curve so the quiet bands still show */
        }
    }
    const float loud = sqrtf((l_e + r_e) / (2.f * FRAME));

    /*
     * Presence: real sound versus room noise, from the raw level. A quiet room reads ~18 LSB RMS,
     * speech nearby ~100, music at a comfortable level 200-500. Everything downstream is scaled by
     * this, so silence stays silent instead of being normalised up to full scale by the gain control.
     */
    const float raw_lsb = loud * 32768.f;
    float presence = (raw_lsb - PRESENCE_FLOOR_LSB) / (PRESENCE_FULL_LSB - PRESENCE_FLOOR_LSB);
    if (presence < 0.f) presence = 0.f;
    if (presence > 1.f) presence = 1.f;
    s_presence += (presence - s_presence) * (presence > s_presence ? 0.5f : 0.05f);   /* fast in, slow out */

    /*
     * Onset: the bass must jump above its recent mean AND rise sharply from the previous frame
     * (a sustained bass note is not a beat), with real sound present. Once a tempo is locked the
     * refractory period stretches to 0.7 of the beat interval, which rejects off-beats and the
     * doubled tempo they produce.
     */
    uint32_t refractory = BEAT_MIN_GAP_MS;
    if (s_gap_ms > 0.f && (uint32_t)(0.7f * s_gap_ms) > refractory) refractory = (uint32_t)(0.7f * s_gap_ms);
    const float kick_e = sqrtf(k_e / (float)FRAME);
    /* his own voice from the speaker is neither a beat nor a talker nor a direction: the levels and
     * the spectrum keep flowing (the dance visuals must not freeze), the detectors hold */
    const bool own_voice = s_muted;
    const bool beat = !own_voice && kick_e > 1.6f * s_kick_mean && kick_e > 1.25f * s_kick_prev && s_presence > 0.25f &&
                      (now_ms - s_last_beat_ms) >= refractory;
    s_kick_prev = kick_e;
    s_kick_mean += (kick_e - s_kick_mean) * (1.f / 30.f);      /* ~0.5 s: spans a beat, not a bar */
    s_bass_prev = bass;
    s_bass_mean += (bass - s_bass_mean) * (1.f / 24.f);
    if (beat) {
        if (s_last_beat_ms) {
            s_beat_gaps[s_gap_idx] = now_ms - s_last_beat_ms;
            s_gap_idx = (s_gap_idx + 1) % 8;
            if (s_gap_n < 8) s_gap_n++;
        }
        s_last_beat_ms = now_ms;
    }
    /*
     * Direction along the microphone axis. The mics are ~40 mm apart: at most ~1.9 samples
     * of delay at 16 kHz, so this needs care. Two estimators:
     *  - a transient (clap, knock) is timed by its rising edge in each channel: the first
     *    crossing of 30 % of that channel's peak, interpolated between samples. The edge
     *    arrives before the room's reflections and before any clipping, so it is the one
     *    robust feature of a clap. The onset may straddle a frame boundary, so the previous
     *    frame's tail is kept.
     *  - a sustained sound (voice) is cross-correlated over lags -3..+3 on the whole frame
     *    with a parabolic peak refinement, and averaged.
     * lag > 0 means the sound reached MIC1 (slot 0) first and MIC2 (slot 1) later.
     */
    float meas_lag = 0.f, meas_conf = 0.f;
    bool measured = false, transient = false;
    micdir_result_t tr;
    if (!own_voice && micdir_frame(&s_micdir, pcm, FRAME, peak, &tr)) {
        measured = transient = true;
        meas_lag = tr.lag;
        meas_conf = tr.balance;
        s_dir_corr = tr.corr;
        s_dir_peak = tr.peak;
        s_dir_level_db = tr.level_db;
    }
    if (!own_voice && !measured && raw_lsb > 60.f) {
        float c[2 * DIR_MAX_LAG + 1], ll = 0.f, rr = 0.f;
        for (int i = 0; i < FRAME; i++) {
            const float l = (float)pcm[2 * i], r = (float)pcm[2 * i + 1];
            ll += l * l; rr += r * r;
        }
        int best = -DIR_MAX_LAG;
        for (int lag = -DIR_MAX_LAG; lag <= DIR_MAX_LAG; lag++) {
            float acc = 0.f;
            const int a = lag < 0 ? -lag : 0, b = lag > 0 ? FRAME - lag : FRAME;
            for (int i = a; i < b; i++) acc += (float)pcm[2 * i] * (float)pcm[2 * (i + lag) + 1];
            c[lag + DIR_MAX_LAG] = acc;
            if (acc > c[best + DIR_MAX_LAG]) best = lag;
        }
        const float norm = sqrtf(ll * rr) + 1.f;
        const float pk = c[best + DIR_MAX_LAG] / norm;
        if (pk > 0.3f) {
            float lagf = (float)best;
            if (best > -DIR_MAX_LAG && best < DIR_MAX_LAG) {
                const float ym = c[best + DIR_MAX_LAG - 1], y0 = c[best + DIR_MAX_LAG], yp = c[best + DIR_MAX_LAG + 1];
                const float den = ym - 2.f * y0 + yp;
                if (den < 0.f) lagf += 0.5f * (ym - yp) / den;
            }
            meas_lag = lagf;
            meas_conf = pk;
            measured = true;
        }
    }
    /*
     * Two cues, fused: the arrival-time difference and the level difference between the
     * mics. In this shell each alone is marginal; together they agreed on every wizard clap.
     * A transient is one clean measurement and takes most of the estimate; a voice averages.
     */
    if (measured || raw_lsb > 60.f) {
        float est = 0.f, w = 0.f;
        if (measured && s_dir_gain != 0.f) {
            float e = (meas_lag - s_dir_off) * s_dir_gain;
            est += e > 1.f ? 1.f : e < -1.f ? -1.f : e;
            w += 1.f;
        }
        if (s_dir_db_gain != 0.f) {
            const float db = transient ? tr.level_db : 20.f * log10f((sqrtf(l_e) + 1e-6f) / (sqrtf(r_e) + 1e-6f));
            float e = (db - s_dir_db_off) * s_dir_db_gain;
            est += e > 1.f ? 1.f : e < -1.f ? -1.f : e;
            w += 1.f;
        }
        if (transient) {
            s_dir_lag = meas_lag;
            s_dir_conf = meas_conf;
        }
        if (w > 0.f) {
            s_dir += (est / w - s_dir) * (transient ? 0.7f : 0.12f);
            if (s_dir > 1.f) s_dir = 1.f;
            if (s_dir < -1.f) s_dir = -1.f;
        }
    } else {
        s_dir *= 0.995f;                 /* fade back to centre in silence, slowly */
    }

    /* sub-bass share of the sound: music with a kick has plenty, conversation almost none */
    const float ratio = loud > 1e-5f ? kick_e / loud : 0.f;
    s_bass_ratio += (ratio - s_bass_ratio) * (1.f / 60.f);

    float bpm = 0.f, regularity = 0.f, tempo_conf = 0.f;
    if (s_gap_n >= 4) {
        /* median interval: one missed or extra beat does not drag the tempo */
        uint32_t sorted[8];
        for (int i = 0; i < s_gap_n; i++) sorted[i] = s_beat_gaps[i];
        for (int i = 1; i < s_gap_n; i++) { const uint32_t v = sorted[i]; int j = i - 1; while (j >= 0 && sorted[j] > v) { sorted[j + 1] = sorted[j]; j--; } sorted[j + 1] = v; }
        const float gap = (float)sorted[s_gap_n / 2];
        if (gap > 0.f) {
            bpm = 60000.f / gap;
            /* accept intervals that are the mean or half/double of it (off-beats) */
            float dev = 0.f;
            for (int i = 0; i < s_gap_n; i++) {
                float g = (float)s_beat_gaps[i];
                float d = fabsf(g - gap);
                const float d2 = fabsf(g * 2.f - gap), dh = fabsf(g * 0.5f - gap);
                if (d2 < d) d = d2;
                if (dh < d) d = dh;
                dev += d / gap;
            }
            dev /= (float)s_gap_n;
            regularity = dev >= 0.5f ? 0.f : 1.f - 2.f * dev;
            /* strict version: how many intervals sit within 12 % of the median (octave-folded) */
            int good = 0;
            for (int i = 0; i < s_gap_n; i++) {
                float g = (float)s_beat_gaps[i];
                if (g > 1.5f * gap) g *= 0.5f;
                if (g < 0.67f * gap) g *= 2.f;
                if (fabsf(g - gap) <= 0.12f * gap) good++;
            }
            tempo_conf = (float)good / (float)s_gap_n;
        }
    }
    /* tempo lock for the refractory period: only while the rhythm looks regular and recent */
    s_gap_ms = (regularity >= 0.5f && bpm >= 50.f && bpm <= 220.f) ? 60000.f / bpm : 0.f;
    if (now_ms - s_last_beat_ms > 2500) { s_gap_ms = 0.f; s_gap_n = 0; s_gap_idx = 0; }   /* rhythm gone: start over */
    const float tot = l_e + r_e;
    const float bal = tot > 1e-6f ? (r_e - l_e) / tot : 0.f;
    s_balance += (bal - s_balance) * 0.2f;

    portENTER_CRITICAL(&s_lock);
    s_feat.active = true;
    s_feat.kick = agc(kick_e, &s_max_kick) * s_presence;
    /*
     * Speech: someone talking makes the mid band (300 Hz - 3 kHz) pulse at syllable rate,
     * 3-8 Hz, without a steady tempo and with almost no sub-bass. The envelope's deviation
     * from its one-second mean, relative to that mean, is the modulation depth; it must hold
     * for ~0.6 s to count and stays counted for 1.5 s after the last syllable.
     */
    s_sp_fast += (mid - s_sp_fast) * 0.6f;              /* ~10 Hz */
    s_sp_slow += (s_sp_fast - s_sp_slow) * 0.1f;        /* ~1 Hz */
    s_sp_mod += (fabsf(s_sp_fast - s_sp_slow) - s_sp_mod) * 0.08f;
    const float sp_depth = s_sp_slow > 1e-4f ? s_sp_mod / s_sp_slow : 0.f;
    /* a voice close to the mics carries plenty of sub-bass (plosives, proximity), so the
     * bass share only rules out real music: a locked dance tempo with a kick under it */
    const bool musical = tempo_conf >= 0.75f && bpm >= 85.f && bpm <= 185.f && s_bass_ratio >= 0.08f;
    /* knocks and claps modulate the mid band too: a timed transient in the last 400 ms is not a syllable */
    if (s_micdir.n != s_dir_seen_n) { s_dir_seen_n = s_micdir.n; s_transient_ms = now_ms; }
    const bool knocking = s_transient_ms && (int32_t)(now_ms - s_transient_ms) < 400;
    const bool talky = !own_voice && s_presence > 0.15f && sp_depth > 0.3f && !musical && !knocking && s_bass_ratio < 0.45f;
    if (talky) { s_sp_on += 2; if (s_sp_on > 60) s_sp_on = 60; s_sp_off = 0; }
    else { if (s_sp_on > 0) s_sp_on--; if (s_sp_off < 1000) s_sp_off++; }
    if (!s_speech && s_sp_on >= 30) s_speech = true;
    if (s_speech && s_sp_off >= 90) s_speech = false;
    s_feat.speech = s_speech;
    s_feat.speech_depth = sp_depth;

    memcpy(s_feat.bands, s_bands, sizeof s_bands);
    s_feat.bass = agc(bass, &s_max_bass) * s_presence;
    s_feat.mid = agc(mid, &s_max_mid) * s_presence;
    s_feat.high = agc(high, &s_max_high) * s_presence;
    s_feat.loud = agc(loud, &s_max_loud) * s_presence;
    s_feat.raw_loud = loud * 32768.f;
    s_feat.peak = (int16_t)(peak > 32767 ? 32767 : peak);
    s_feat.balance = s_balance;
    if (beat) s_feat.beat_count++;
    s_feat.last_beat_ms = s_last_beat_ms;
    s_feat.bpm = bpm;
    s_feat.regularity = regularity;
    s_feat.tempo_conf = tempo_conf;
    s_feat.bass_ratio = s_bass_ratio;
    s_feat.rms_l = sqrtf(l_e / (float)FRAME) * 32768.f;
    s_feat.rms_r = sqrtf(r_e / (float)FRAME) * 32768.f;
    s_feat.dir = s_dir;
    s_feat.dir_conf = s_dir_conf;
    s_feat.dir_lag = s_dir_lag;
    s_feat.dir_n = s_micdir.n;
    s_feat.dir_corr = s_dir_corr;
    s_feat.dir_peak = s_dir_peak;
    s_feat.dir_level_db = s_dir_level_db;
    s_feat.dir_loud = s_micdir.loud;
    s_feat.dir_pre = s_micdir.pre;
    portEXIT_CRITICAL(&s_lock);
}

static void audio_task(void *arg)
{
    static int16_t pcm[FRAME * 2];
    while (s_run) {
        const int r = esp_codec_dev_read(s_dev, pcm, sizeof(pcm));
        if (r != ESP_CODEC_DEV_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        const int64_t t0 = esp_timer_get_time();
        analyse(pcm, (uint32_t)(t0 / 1000));
        const uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
        portENTER_CRITICAL(&s_lock);
        s_feat.cpu_us = us;
        portEXIT_CRITICAL(&s_lock);
    }
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t audio_start(void)
{
    if (s_run) return ESP_OK;
    static bool tables;
    if (!tables) {
        tables_init();
        tables = true;
    }

    /* Full duplex like the Waveshare BSP: the RX side alone left the ES7210's data line silent. */
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_EYES_AUDIO_I2S_NUM, I2S_ROLE_MASTER);
    chan.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, &s_tx, &s_rx), TAG, "i2s channel");
    const i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK,
            .bclk = BOARD_I2S_BCLK,
            .ws = BOARD_I2S_LRCK,
            .dout = BOARD_I2S_DOUT,
            .din = BOARD_I2S_DIN,
            .invert_flags = { 0 },
        },
    };
    esp_err_t err = i2s_channel_init_std_mode(s_tx, &std);
    if (err == ESP_OK) err = i2s_channel_enable(s_tx);
    if (err == ESP_OK) err = i2s_channel_init_std_mode(s_rx, &std);
    if (err == ESP_OK) err = i2s_channel_enable(s_rx);
    if (err != ESP_OK) {
        i2s_del_channel(s_tx);
        i2s_del_channel(s_rx);
        s_tx = s_rx = NULL;
        ESP_RETURN_ON_ERROR(err, TAG, "i2s std");
    }

    audio_codec_i2s_cfg_t i2s_cfg = { .port = CONFIG_EYES_AUDIO_I2S_NUM, .rx_handle = s_rx, .tx_handle = s_tx };
    s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    /* The I2C control interface registers the codec on the shared bus. Keep it
     * for good: removing the device while the touch task has a transaction in
     * flight fails ("Wrong I2C status") and leaks the handle. */
    if (!s_ctrl_if) {
        audio_codec_i2c_cfg_t i2c_cfg = { .port = BOARD_I2C_PORT, .addr = ES7210_CODEC_DEFAULT_ADDR, .bus_handle = i2c_bus_get() };
        s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    }
    es7210_codec_cfg_t es_cfg = { .ctrl_if = s_ctrl_if, .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 };
    s_codec_if = es7210_codec_new(&es_cfg);
    esp_codec_dev_cfg_t dev_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = s_codec_if, .data_if = s_data_if };
    s_dev = (s_data_if && s_ctrl_if && s_codec_if) ? esp_codec_dev_new(&dev_cfg) : NULL;
    if (!s_dev) {
        ESP_LOGE(TAG, "ES7210 setup failed (data_if %p ctrl_if %p codec_if %p)", s_data_if, s_ctrl_if, s_codec_if);
        audio_stop();
        return ESP_FAIL;
    }
    esp_codec_dev_sample_info_t fs = { .sample_rate = SAMPLE_RATE, .channel = 2, .bits_per_sample = 16 };
    if (esp_codec_dev_set_in_gain(s_dev, (float)s_gain_db) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_open(s_dev, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "ES7210 open failed");
        audio_stop();
        return ESP_FAIL;
    }
    s_opened = true;

    /* the speaker: ES8311 on the shared I2S data interface; failure here is not fatal */
    if (!s_spk_ctrl_if) {
        audio_codec_i2c_cfg_t i2c_cfg = { .port = BOARD_I2C_PORT, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = i2c_bus_get() };
        s_spk_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    }
    es8311_codec_cfg_t spk_cfg = {
        .ctrl_if = s_spk_ctrl_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,                    /* the amplifier is switched here, only while something plays */
        .use_mclk = true,
        .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3, .pa_gain = 6.0 },
        .mclk_div = 256,
    };
    s_spk_codec_if = s_spk_ctrl_if ? es8311_codec_new(&spk_cfg) : NULL;
    esp_codec_dev_cfg_t spk_dev_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = s_spk_codec_if, .data_if = s_data_if };
    s_spk = s_spk_codec_if ? esp_codec_dev_new(&spk_dev_cfg) : NULL;
    if (s_spk) {
        esp_codec_dev_sample_info_t ofs = { .sample_rate = SAMPLE_RATE, .channel = 2, .bits_per_sample = 16 };
        if (esp_codec_dev_open(s_spk, &ofs) == ESP_CODEC_DEV_OK) {
            s_spk_opened = true;
            esp_codec_dev_set_out_vol(s_spk, (float)s_volume);
        } else {
            ESP_LOGW(TAG, "ES8311 open failed: no speaker");
        }
    } else {
        ESP_LOGW(TAG, "ES8311 setup failed: no speaker");
    }
    gpio_config_t pa = { .pin_bit_mask = 1ULL << BOARD_PA_EN, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&pa);
    gpio_set_level(BOARD_PA_EN, 0);

    portENTER_CRITICAL(&s_lock);
    memset(&s_feat, 0, sizeof(s_feat));
    portEXIT_CRITICAL(&s_lock);
    s_bass_mean = 0.f;
    s_bass_prev = 0.f;
    s_kick_mean = s_kick_prev = 0.f;
    s_bass_ratio = 0.f;
    s_dir = s_dir_conf = s_dir_lag = 0.f;
    micdir_reset(&s_micdir);
    s_dir_corr = 0.f;
    s_dir_peak = 0;
    s_dir_level_db = 0.f;
    s_lp_x1 = s_lp_x2 = s_lp_y1 = s_lp_y2 = 0.f;
    s_max_kick = 1e-3f;
    s_presence = 0.f;
    for (int b = 0; b < 16; b++) { s_band_max[b] = 1e-3f; s_bands[b] = 0.f; }
    s_sp_fast = s_sp_slow = s_sp_mod = 0.f;
    s_sp_on = s_sp_off = 0;
    s_speech = false;
    s_gap_ms = 0.f;
    s_last_beat_ms = 0;
    s_gap_n = s_gap_idx = 0;
    s_max_bass = s_max_mid = s_max_high = s_max_loud = 1e-3f;

    s_run = true;
    if (xTaskCreatePinnedToCore(audio_task, "audio", 6144, NULL, 6, &s_task, 0) != pdPASS) {
        s_run = false;
        audio_stop();
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "microphones on: ES7210, %d Hz stereo, %d-sample frames, gain %d dB", SAMPLE_RATE, FRAME, s_gain_db);
    return ESP_OK;
}

void audio_stop(void)
{
    s_run = false;
    /* let the task fall out of its read loop */
    for (int i = 0; i < 50 && s_task; i++) vTaskDelay(pdMS_TO_TICKS(10));
    bool rx_enabled = s_rx != NULL;
    gpio_set_level(BOARD_PA_EN, 0);
    if (s_spk) {
        if (s_spk_opened) esp_codec_dev_close(s_spk);
        esp_codec_dev_delete(s_spk);
        s_spk = NULL;
        s_spk_opened = false;
    }
    if (s_spk_codec_if) { audio_codec_delete_codec_if(s_spk_codec_if); s_spk_codec_if = NULL; }
    if (s_dev) {
        /* closing an opened codec device disables the I2S channel through the data interface */
        if (s_opened) {
            esp_codec_dev_close(s_dev);
            rx_enabled = false;
        }
        esp_codec_dev_delete(s_dev);
        s_dev = NULL;
        s_opened = false;
    }
    if (s_codec_if) { audio_codec_delete_codec_if(s_codec_if); s_codec_if = NULL; }
    if (s_data_if) { audio_codec_delete_data_if(s_data_if); s_data_if = NULL; }
    if (s_rx) {
        if (rx_enabled) i2s_channel_disable(s_rx);
        i2s_del_channel(s_rx);
        s_rx = NULL;
    }
    if (s_tx) {
        i2s_channel_disable(s_tx);
        i2s_del_channel(s_tx);
        s_tx = NULL;
    }
    portENTER_CRITICAL(&s_lock);
    s_feat.active = false;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "microphones off");
}

esp_err_t audio_write(const int16_t *mono, int n)
{
    if (!s_spk_opened) return ESP_ERR_INVALID_STATE;
    while (n > 0) {
        const int k = n < 320 ? n : 320;
        for (int i = 0; i < k; i++) s_stereo[2 * i] = s_stereo[2 * i + 1] = mono[i];
        if (esp_codec_dev_write(s_spk, s_stereo, k * 4) != ESP_CODEC_DEV_OK) return ESP_FAIL;
        mono += k;
        n -= k;
    }
    return ESP_OK;
}

void audio_set_volume(int pct)
{
    s_volume = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    if (s_spk_opened) esp_codec_dev_set_out_vol(s_spk, (float)s_volume);
}

void audio_pa(bool on)
{
    gpio_set_level(BOARD_PA_EN, on);
}

void audio_set_muted(bool muted)
{
    s_muted = muted;
}

void audio_set_gain_db(int db)
{
    s_gain_db = db;
}

int audio_gain_db(void)
{
    return s_gain_db;
}

void audio_set_dir_cal(const mic_cal_t *cal)
{
    if (cal && cal->valid) {
        s_dir_off = cal->offset;
        s_dir_gain = cal->gain;
        s_dir_db_off = cal->db_offset;
        s_dir_db_gain = cal->db_gain;
    } else {
        s_dir_off = 0.f;
        s_dir_gain = 0.5f;
        s_dir_db_off = 0.f;
        s_dir_db_gain = 0.f;
    }
}

bool audio_running(void)
{
    return s_run;
}

void audio_get_features(audio_features_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_feat;
    portEXIT_CRITICAL(&s_lock);
}
