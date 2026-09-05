#include "behavior.h"

#include <math.h>
#include <string.h>
#include "sdkconfig.h"

#ifndef CONFIG_EYES_SHAKE_MG
#define CONFIG_EYES_SHAKE_MG 350
#endif
#ifndef CONFIG_EYES_SNIFF_INTERVAL_S
#define CONFIG_EYES_SNIFF_INTERVAL_S 20
#endif
#ifndef CONFIG_EYES_GRAVITY_FACE
#define CONFIG_EYES_GRAVITY_FACE 1
#endif

#define FACE_TILT_ON_G      0.25f    /* lateral gravity needed before the face follows (about 14 degrees) */
#define FACE_TILT_OFF_G     0.15f    /* ...and below this it comes back upright */
#define FACE_TAU_MS         70.f     /* first-order approach to the gravity angle */
#define FACE_SLEW_DEG_S     720.f    /* rate cap on that approach */
#define FACE_DEADBAND_DEG   1.5f
#define GRAV_TAU_MS         60.f     /* gravity vector smoothing */
#define SHAKE_TAU_MS        150.f    /* shake envelope smoothing */

#define SHAKE_ON_G          ((float)CONFIG_EYES_SHAKE_MG / 1000.f)
#define DIZZY_AFTER_MS      900.f
#define KO_AFTER_MS         3600.f
#define KO_DURATION_MS      8000
#define GROGGY_MS           3000
#define FACE_DOWN_MS        1500
#define WAKING_MS           500
#define SNIFF_MS            3200
#define MUSIC_MIN_BEATS     5
#define MUSIC_QUIET_MS      12000
#define GAZE_PX_X           12.f
#define GAZE_PX_Y           8.f

const char *behavior_state_name(behavior_state_t s)
{
    static const char *n[] = { "idle", "dizzy", "knocked-out", "groggy", "face-down", "waking", "music", "unimpressed" };
    return s <= BEH_UNIMPRESSED ? n[s] : "?";
}

void behavior_init(behavior_t *b, uint32_t now_ms)
{
    memset(b, 0, sizeof(*b));
    b->state_since_ms = now_ms;
    b->gz = 1.f;
    b->next_sniff_ms = now_ms + 5000;
    b->energy = 0.6f;
    b->mood_tick_ms = now_ms;
    b->rng = 0x2545F491u ^ now_ms;
}

float behavior_energy(const behavior_t *b)
{
    return b->energy;
}

static float frand(behavior_t *b)
{
    uint32_t x = b->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    b->rng = x;
    return (float)(x & 0xFFFFFF) / 16777216.f;
}

/* Mood: energy rises when he is handled or touched, sinks with time, and wanders a little. */
static void feel_mood(behavior_t *b, const behavior_in_t *in, uint32_t now_ms)
{
    if (now_ms - b->mood_tick_ms < 1000) return;
    b->mood_tick_ms = now_ms;
    float e = b->energy;
    if (b->shake > 0.08f) e += 0.02f;
    if (in->user_interacting) e += 0.015f;
    e += (0.4f - e) * 0.01f;               /* slow drift toward a calm baseline */
    e += (frand(b) - 0.5f) * 0.03f;        /* wandering */
    if (e < 0.f) e = 0.f;
    if (e > 1.f) e = 1.f;
    b->energy = e;
}

static void enter(behavior_t *b, behavior_state_t s, uint32_t now_ms)
{
    b->state = s;
    b->state_since_ms = now_ms;
}

static void feel_motion(behavior_t *b, const behavior_in_t *in, uint32_t now_ms)
{
    if (!in->have_accel || in->accel_ms == b->last_accel_ms) return;
    const uint32_t dt = b->last_accel_ms ? in->accel_ms - b->last_accel_ms : 50;
    b->last_accel_ms = in->accel_ms;

    float g[3], sg[3];
    imu_cal_apply(in->cal, in->accel, g);
    imu_cal_screen(in->cal, g, sg);
    const float mag = sqrtf(sg[0] * sg[0] + sg[1] * sg[1] + sg[2] * sg[2]);

    /* shake: how far the magnitude departs from 1 g, smoothed over ~0.3 s */
    const float dev = fabsf(mag - 1.f);
    const float fdt = (float)dt;
    b->shake += (dev - b->shake) * (fdt / (fdt + SHAKE_TAU_MS));
    if (b->shake > SHAKE_ON_G) {
        b->shake_time_ms += (float)dt;
    } else {
        b->shake_time_ms -= (float)dt * 0.6f;
        if (b->shake_time_ms < 0.f) b->shake_time_ms = 0.f;
    }

    /* gravity direction, smoothed (used for gaze and face-down) */
    const float k = fdt / (fdt + GRAV_TAU_MS);
    b->gx += (sg[0] - b->gx) * k;
    b->gy += (sg[1] - b->gy) * k;
    b->gz += (sg[2] - b->gz) * k;

    if (b->gz < -0.6f) {
        if (!b->face_down_since_ms) b->face_down_since_ms = now_ms;
    } else {
        b->face_down_since_ms = 0;
    }
}

static bool music_detected(const audio_features_t *a)
{
    return a->active && a->beat_count >= MUSIC_MIN_BEATS && a->bpm >= 60.f && a->bpm <= 190.f && a->regularity >= 0.6f;
}

void behavior_update(behavior_t *b, const behavior_in_t *in, uint32_t now_ms, behavior_out_t *out)
{
    memset(out, 0, sizeof(*out));
    out->override_anim = -1;
    feel_motion(b, in, now_ms);
    feel_mood(b, in, now_ms);
    const bool tapped = in->tap_count != b->taps_seen;
    b->taps_seen = in->tap_count;

    const bool shaking_hard = b->shake_time_ms >= DIZZY_AFTER_MS;
    const bool face_down = b->face_down_since_ms && (now_ms - b->face_down_since_ms) >= FACE_DOWN_MS;
    const uint32_t in_state = now_ms - b->state_since_ms;

    /* --- transitions, highest priority first --- */
    if (b->state != BEH_KNOCKED_OUT && b->shake_time_ms >= KO_AFTER_MS) {
        enter(b, BEH_KNOCKED_OUT, now_ms);
        b->shake_time_ms = 0.f;
        b->sniffing = false;
    } else {
        switch (b->state) {
        case BEH_KNOCKED_OUT:
            if (in_state >= KO_DURATION_MS) enter(b, BEH_GROGGY, now_ms);
            break;
        case BEH_GROGGY:
            if (in_state >= GROGGY_MS) enter(b, BEH_IDLE, now_ms);
            break;
        case BEH_DIZZY:
            if (!shaking_hard && b->shake_time_ms < 200.f) enter(b, BEH_IDLE, now_ms);
            break;
        case BEH_FACE_DOWN:
            if (!face_down && !b->face_down_since_ms) enter(b, BEH_WAKING, now_ms);
            break;
        case BEH_WAKING:
            if (in_state >= WAKING_MS) enter(b, BEH_IDLE, now_ms);
            break;
        case BEH_MUSIC:
            if (shaking_hard) { enter(b, BEH_DIZZY, now_ms); break; }
            if (face_down) { enter(b, BEH_FACE_DOWN, now_ms); break; }
            if (in->audio.active && in->audio.loud > 0.08f) b->music_quiet_since_ms = now_ms;
            if (tapped || !in->audio.active || now_ms - b->music_quiet_since_ms >= MUSIC_QUIET_MS) {
                enter(b, BEH_IDLE, now_ms);
                /* a tap means "enough", so stay off the music for a good while */
                b->next_sniff_ms = now_ms + (tapped ? 6u : 1u) * CONFIG_EYES_SNIFF_INTERVAL_S * 1000u;
            }
            break;
        case BEH_UNIMPRESSED:
            if (in_state >= 4500 || tapped) {
                enter(b, BEH_IDLE, now_ms);
                b->next_sniff_ms = now_ms + 6u * CONFIG_EYES_SNIFF_INTERVAL_S * 1000u;
            }
            break;
        case BEH_IDLE:
        default:
            if (shaking_hard) enter(b, BEH_DIZZY, now_ms);
            else if (face_down) enter(b, BEH_FACE_DOWN, now_ms);
            else if (b->sniffing && music_detected(&in->audio)) {
                b->sniffing = false;
                b->music_quiet_since_ms = now_ms;
                /* roll the reaction against his mood */
                const float r = frand(b);
                if (r < 0.12f + 0.25f * (1.f - b->energy)) {
                    enter(b, BEH_UNIMPRESSED, now_ms);
                } else {
                    b->music_headphones = frand(b) < 0.2f + 0.6f * b->energy;
                    enter(b, BEH_MUSIC, now_ms);
                }
            }
            break;
        }
    }

    /* --- music sniffing: a short listen every so often while idle --- */
    if (in->mic_available && CONFIG_EYES_SNIFF_INTERVAL_S > 0 && b->state == BEH_IDLE && !in->user_interacting) {
        if (!b->sniffing && (int32_t)(now_ms - b->next_sniff_ms) >= 0) {
            b->sniffing = true;
            b->sniff_start_ms = now_ms;
        }
        if (b->sniffing && now_ms - b->sniff_start_ms >= SNIFF_MS) {
            b->sniffing = false;
            b->next_sniff_ms = now_ms + CONFIG_EYES_SNIFF_INTERVAL_S * 1000u;
        }
    } else {
        b->sniffing = false;
    }
    out->want_mic = b->sniffing || b->state == BEH_MUSIC;

    /* --- outputs --- */
    switch (b->state) {
    case BEH_DIZZY:       out->override_anim = ANIM_DIZZY; break;
    case BEH_KNOCKED_OUT: out->knocked_out = true; out->override_anim = ANIM_SLEEPING; break;
    case BEH_GROGGY:      out->override_anim = ANIM_SLEEPY; break;
    case BEH_FACE_DOWN:   out->override_anim = ANIM_SLEEPING; out->zz = true; break;
    case BEH_WAKING:      out->override_anim = ANIM_SURPRISED; break;
    case BEH_MUSIC:       out->override_anim = ANIM_DANCE; out->headphones = b->music_headphones; break;
    case BEH_UNIMPRESSED: out->override_anim = ANIM_ANNOYED; break;
    default: break;
    }

    /* gravity face: keep the face upright against gravity, like a badge on a wheel */
    if (CONFIG_EYES_GRAVITY_FACE && in->have_accel) {
        const float lat = sqrtf(b->gx * b->gx + b->gy * b->gy);
        if (lat > FACE_TILT_ON_G) {
            b->face_target = atan2f(b->gx, -b->gy) * 57.2957795f;
        } else if (lat < FACE_TILT_OFF_G) {
            b->face_target = 0.f;   /* lying flat: come back upright */
        }
        const uint32_t dt = b->face_ms ? now_ms - b->face_ms : 16;
        b->face_ms = now_ms;
        float diff = b->face_target - b->face_angle;
        while (diff > 180.f) diff -= 360.f;
        while (diff < -180.f) diff += 360.f;
        /* ease toward the target, but never faster than the slew cap */
        const float fdt = (float)dt;
        float step = fabsf(diff) * (fdt / (fdt + FACE_TAU_MS));
        const float cap = FACE_SLEW_DEG_S * fdt / 1000.f;
        if (step > cap) step = cap;
        if (fabsf(diff) <= step || fabsf(diff) < 0.05f) b->face_angle = b->face_target;
        else b->face_angle += diff > 0.f ? step : -step;
        while (b->face_angle > 180.f) b->face_angle -= 360.f;
        while (b->face_angle < -180.f) b->face_angle += 360.f;
        out->face_angle_deg = fabsf(b->face_angle) < FACE_DEADBAND_DEG ? 0.f : b->face_angle;
    }

    /* gravity gaze: the eyes slide toward the low side (not while spinning or out cold) */
    if (in->have_accel && (b->state == BEH_IDLE || b->state == BEH_MUSIC || b->state == BEH_WAKING)) {
        float bx = -b->gx, by = -b->gy;
        if (bx > 1.f) bx = 1.f;
        if (bx < -1.f) bx = -1.f;
        if (by > 1.f) by = 1.f;
        if (by < -1.f) by = -1.f;
        /* with the face following gravity, the residual gaze is what is left after the turn */
        if (out->face_angle_deg != 0.f) {
            const float r = -out->face_angle_deg * 0.01745329f;
            const float c = cosf(r), sn = sinf(r);
            const float rx = bx * c - by * sn, ry = bx * sn + by * c;
            bx = rx; by = ry;
        }
        /* slight handling: a small wobble proportional to the motion */
        float wob = b->shake > 0.03f ? (b->shake - 0.03f) * 40.f : 0.f;
        if (wob > 6.f) wob = 6.f;
        const float ph = (float)(now_ms % 1000) * 0.0062831853f * 6.f;    /* 6 Hz */
        const float wx = sinf(ph) * wob, wy = cosf(ph * 1.3f) * wob * 0.6f;
        for (int e = 0; e < 2; e++) {
            out->env[e].dx = (int32_t)((bx * GAZE_PX_X + wx) * 65536.f);
            out->env[e].dy = (int32_t)((by * GAZE_PX_Y + wy) * 65536.f);
        }
    }
}
