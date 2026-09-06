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
#define SNIFF_MS            5200     /* long enough for eight beats at 100 bpm */
#define MUSIC_MIN_BEATS     8
#define MUSIC_QUIET_MS      6000     /* no beat for this long: the music is over */
#define MUSIC_BASS_RATIO    0.08f    /* sub-bass share: phone speakers give 0.1-0.2 on psytrance, so only reject near-zero */
#define MUSIC_TEMPO_CONF    0.75f    /* the steady tempo is what tells a kick drum from a conversation */
#define GAZE_PX_X           12.f
#define GAZE_PX_Y           8.f
#define VOICE_LEAN_PX       55.f     /* how far the eyes lean toward a voice at the end of the mic axis */

const char *behavior_state_name(behavior_state_t s)
{
    static const char *n[] = { "idle", "dizzy", "knocked-out", "groggy", "face-down", "waking", "music", "unimpressed", "listening", "carried", "startled", "poked", "petted" };
    return s <= BEH_UNIMPRESSED ? n[s] : "?";
}

void behavior_init(behavior_t *b, uint32_t now_ms)
{
    memset(b, 0, sizeof(*b));
    b->state_since_ms = now_ms;
    b->gz = 1.f;
    b->next_sniff_ms = now_ms + 5000;
    b->energy = 0.6f;
    b->valence = 0.1f;
    b->idle_anim = ANIM_NEUTRAL;
    b->idle_roll_ms = now_ms + 20000;
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

/*
 * Mood, once a second. Two numbers, Vector's way: stimulation (energy) rises with anything
 * happening to him and sinks with quiet; valence is how well he has been treated, pushed up
 * by petting, company, music and food when hungry, down by shaking, knocks, pokes and being
 * left face down, and it drifts back toward neutral slowly.
 */
static void feel_mood(behavior_t *b, const behavior_in_t *in, uint32_t now_ms)
{
    if (now_ms - b->mood_tick_ms < 1000) return;
    b->mood_tick_ms = now_ms;
    float e = b->energy, v = b->valence;
    if (b->shake > 0.08f) { e += 0.03f; if (b->state != BEH_MUSIC && !in->dancing) v -= 0.06f; }
    if (in->user_interacting) e += 0.015f;
    if (in->audio.active && in->audio.speech) { e += 0.01f; v += 0.01f; }
    switch (b->state) {
    case BEH_MUSIC:   e += 0.02f; v += 0.01f; break;
    case BEH_PETTED:  e += 0.02f; v += 0.05f; break;
    case BEH_CARRIED: v += 0.01f; break;
    case BEH_FACE_DOWN: v -= 0.02f; break;
    case BEH_KNOCKED_OUT: v -= 0.03f; break;
    default: break;
    }
    e += (0.4f - e) * 0.01f;               /* slow drift toward a calm baseline */
    e += (frand(b) - 0.5f) * 0.03f;        /* wandering */
    v += (0.f - v) * 0.004f;               /* grudges and gratitude fade over minutes */
    if (e < 0.f) e = 0.f;
    if (e > 1.f) e = 1.f;
    if (v < -1.f) v = -1.f;
    if (v > 1.f) v = 1.f;
    b->energy = e;
    b->valence = v;
}

float behavior_valence(const behavior_t *b)
{
    return b->valence;
}

void behavior_feel(behavior_t *b, float valence_delta)
{
    b->valence += valence_delta;
    if (b->valence < -1.f) b->valence = -1.f;
    if (b->valence > 1.f) b->valence = 1.f;
}

/* the idle face of the moment, from the mood: content when treated well, sour when not, sleepy when flat */
static void roll_idle_face(behavior_t *b, uint32_t now_ms)
{
    const float r = frand(b);
    const float e = b->energy, v = b->valence;
    anim_id_t a = ANIM_NEUTRAL;
    if (v > 0.35f) {
        if (r < 0.4f) a = ANIM_HAPPY; else if (r < 0.6f) a = e > 0.6f ? ANIM_EXCITED : ANIM_HAPPY;
        else if (r < 0.75f) a = ANIM_LOVE; else if (r < 0.9f) a = ANIM_NEUTRAL; else a = ANIM_CURIOUS;
    } else if (v < -0.35f) {
        if (r < 0.35f) a = ANIM_ANNOYED; else if (r < 0.55f) a = ANIM_SKEPTICAL; else if (r < 0.75f) a = ANIM_SAD;
        else if (r < 0.9f) a = ANIM_ANGRY; else a = ANIM_SQUINT;
    } else if (e < 0.25f) {
        if (r < 0.4f) a = ANIM_SLEEPY; else if (r < 0.7f) a = ANIM_BORED; else a = ANIM_NEUTRAL;
    } else if (e > 0.7f) {
        if (r < 0.35f) a = ANIM_CURIOUS; else if (r < 0.55f) a = ANIM_LOOK_AROUND; else if (r < 0.75f) a = ANIM_EXCITED;
        else if (r < 0.9f) a = ANIM_HAPPY; else a = ANIM_THINKING;
    } else {
        if (r < 0.45f) a = ANIM_NEUTRAL; else if (r < 0.6f) a = ANIM_CURIOUS; else if (r < 0.72f) a = ANIM_THINKING;
        else if (r < 0.82f) a = ANIM_LOOK_AROUND; else if (r < 0.9f) a = ANIM_HAPPY; else if (r < 0.95f) a = ANIM_BORED; else a = ANIM_SHY;
    }
    b->idle_anim = a;
    b->idle_roll_ms = now_ms + 15000 + (uint32_t)(frand(b) * 40000.f);
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

    /*
     * Handling. Still versus moving from the smoothed shake: a pick-up is motion that starts
     * after a long rest, a put-down is rest that follows a stretch of motion. A walking rhythm
     * is the high-passed |a| crossing zero at 1.5-2.5 Hz with a decent swing for a few seconds.
     * A body tap is a single sharp spike in |a| between two samples with nothing before or
     * after, and no finger on the glass.
     */
    const bool moving = b->shake > 0.025f;
    if (moving) {
        if (!b->moving_since_ms) {
            b->moving_since_ms = now_ms;
            if (b->was_resting) b->pending = BEH_EV_PICKED_UP;
        }
        b->still_since_ms = 0;
        b->was_resting = false;
    } else {
        if (!b->still_since_ms) {
            b->still_since_ms = now_ms;
            if (b->moving_since_ms && now_ms - b->moving_since_ms > 1200) b->pending = BEH_EV_PUT_DOWN;
        }
        b->moving_since_ms = 0;
        if (now_ms - b->still_since_ms > 3000) b->was_resting = true;
    }
    /* walking rhythm */
    const float hp_in = mag - 1.f;
    const float hp = 0.9f * (b->hp_mag + hp_in - b->hp_prev);      /* one-pole high-pass ~1 Hz at 100 Hz */
    b->hp_prev = hp_in;
    const bool crossed = (hp > 0.f) != (b->hp_mag > 0.f);
    b->hp_mag = hp;
    b->cross_amp += (fabsf(hp) - b->cross_amp) * 0.05f;
    if (crossed) {
        const float gap = b->last_cross_ms ? (float)(now_ms - b->last_cross_ms) : 0.f;
        b->last_cross_ms = now_ms;
        if (gap > 0.f) b->cross_gap_ms += (gap - b->cross_gap_ms) * 0.3f;
    }
    /* two crossings per step: 1.5-2.5 Hz is a gap of 200-333 ms */
    const bool rhythmic = b->cross_gap_ms > 180.f && b->cross_gap_ms < 360.f && b->cross_amp > 0.03f && b->cross_amp < 0.4f &&
                          now_ms - b->last_cross_ms < 500;
    if (rhythmic) { if (!b->rhythm_since_ms) b->rhythm_since_ms = now_ms; }
    else if (now_ms - b->last_cross_ms > 1500 || b->cross_amp < 0.02f) b->rhythm_since_ms = 0;
    /* body tap: a spike, isolated */
    const float jump = fabsf(mag - b->prev_mag);
    b->prev_mag = mag;
    if (jump > 0.15f && b->shake < 0.05f && !in->user_interacting && now_ms - b->spike_ms > 600) {
        b->spike_ms = now_ms;
        b->spike_side = sg[0] > 0.f ? 1.f : -1.f;
        b->pending = BEH_EV_BODY_TAP;
    }
}

/* the face while someone talks: mostly curious, sometimes thinking, doubtful, amused */
static void roll_listen_face(behavior_t *b, uint32_t now_ms)
{
    const float r = frand(b);
    if (r < 0.35f) b->listen_anim = ANIM_CURIOUS;
    else if (r < 0.55f) b->listen_anim = ANIM_THINKING;
    else if (r < 0.70f) b->listen_anim = ANIM_SKEPTICAL;
    else if (r < 0.80f) b->listen_anim = ANIM_LOOK_AROUND;
    else if (r < 0.90f) b->listen_anim = ANIM_HAPPY;
    else b->listen_anim = ANIM_SURPRISED;
    b->listen_roll_ms = now_ms;
}

static bool music_detected(const audio_features_t *a)
{
    /* a steady tempo in the dance range, carried by a kick drum: conversation has neither */
    return a->active && a->beat_count >= MUSIC_MIN_BEATS && a->bpm >= 85.f && a->bpm <= 185.f &&
           a->tempo_conf >= MUSIC_TEMPO_CONF && a->bass_ratio >= MUSIC_BASS_RATIO;
}

void behavior_update(behavior_t *b, const behavior_in_t *in, uint32_t now_ms, behavior_out_t *out)
{
    memset(out, 0, sizeof(*out));
    out->override_anim = -1;
    feel_motion(b, in, now_ms);
    feel_mood(b, in, now_ms);
    const bool tapped = in->tap_count != b->taps_seen;
    b->taps_seen = in->tap_count;
    out->event = b->pending;
    out->tap_side = b->spike_side;
    b->pending = BEH_EV_NONE;
    const bool stroked = in->stroke_count != b->strokes_seen;
    b->strokes_seen = in->stroke_count;
    if (stroked) b->last_stroke_ms = now_ms;

    /* dancing: the owner is dancing with him, so shaking is part of it, never dizziness or a knock-out */
    const bool dancing = b->state == BEH_MUSIC || in->dancing;
    if (dancing) b->shake_time_ms = 0.f;
    const bool shaking_hard = !dancing && b->shake_time_ms >= DIZZY_AFTER_MS;
    const bool face_down = b->face_down_since_ms && (now_ms - b->face_down_since_ms) >= FACE_DOWN_MS;
    const uint32_t in_state = now_ms - b->state_since_ms;

    /* --- transitions, highest priority first --- */
    if (!dancing && b->state != BEH_KNOCKED_OUT && b->shake_time_ms >= KO_AFTER_MS) {
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
            /* still music while the beats keep coming with a kick under them */
            if (in->audio.active && in->audio.last_beat_ms && now_ms - in->audio.last_beat_ms < 2500 &&
                in->audio.bass_ratio >= MUSIC_BASS_RATIO * 0.6f) {
                b->music_quiet_since_ms = now_ms;
            }
            /* touch never stops the dance: a stroke adds a slow sway, a tap changes nothing */
            if (stroked) out->dance_flourish = 2;
            if (!in->audio.active || now_ms - b->music_quiet_since_ms >= MUSIC_QUIET_MS) {
                enter(b, BEH_IDLE, now_ms);
                b->next_sniff_ms = now_ms + CONFIG_EYES_SNIFF_INTERVAL_S * 1000u;
            }
            break;
        case BEH_LISTENING:
            if (shaking_hard) { enter(b, BEH_DIZZY, now_ms); break; }
            if (face_down) { enter(b, BEH_FACE_DOWN, now_ms); break; }
            if (out->event == BEH_EV_BODY_TAP) { enter(b, BEH_STARTLED, now_ms); break; }
            if (in->audio.active && in->audio.speech) b->speech_last_ms = now_ms;
            if (!in->audio.active || now_ms - b->speech_last_ms > 2000 || tapped) { enter(b, BEH_IDLE, now_ms); break; }
            if (now_ms - b->listen_roll_ms > 4500) roll_listen_face(b, now_ms);
            break;
        case BEH_POKED:
            if (shaking_hard) { enter(b, BEH_DIZZY, now_ms); break; }
            if (in_state >= (b->poked_eye ? 1500u : 1600u)) enter(b, BEH_IDLE, now_ms);
            break;
        case BEH_PETTED:
            if (shaking_hard) { enter(b, BEH_DIZZY, now_ms); break; }
            if (now_ms - b->last_stroke_ms > 3000) enter(b, BEH_IDLE, now_ms);
            break;
        case BEH_CARRIED:
            if (shaking_hard) { enter(b, BEH_DIZZY, now_ms); break; }
            if (!b->rhythm_since_ms && in_state > 2000) enter(b, BEH_IDLE, now_ms);
            break;
        case BEH_STARTLED:
            if (shaking_hard) { enter(b, BEH_DIZZY, now_ms); break; }
            if (in_state >= 1400) enter(b, BEH_IDLE, now_ms);
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
            else if (out->event == BEH_EV_BODY_TAP) enter(b, BEH_STARTLED, now_ms);
            else if (b->rhythm_since_ms && now_ms - b->rhythm_since_ms > 4000) enter(b, BEH_CARRIED, now_ms);
            else if (in->dancing) {
                /* the dance he was put in by hand: same rule */
                if (stroked) out->dance_flourish = 2;
            }
            else if (stroked && in->stroke_forehead) {
                enter(b, BEH_PETTED, now_ms);
            }
            else if (tapped) {
                /* a poke: a short reaction whose face depends on his mood; a poke in the eye closes it */
                const float r = frand(b);
                b->poked_eye = in->poke_eye;
                if (b->valence < -0.3f) b->poke_anim = r < 0.5f ? ANIM_ANNOYED : r < 0.8f ? ANIM_ANGRY : ANIM_SKEPTICAL;
                else if (r < 0.25f) b->poke_anim = ANIM_SURPRISED;
                else if (r < 0.45f) b->poke_anim = b->energy > 0.5f ? ANIM_HAPPY : ANIM_ANNOYED;
                else if (r < 0.6f) b->poke_anim = ANIM_CONFUSED;
                else if (r < 0.75f) b->poke_anim = ANIM_SQUINT;
                else if (r < 0.87f) b->poke_anim = ANIM_SKEPTICAL;
                else b->poke_anim = ANIM_WINK;
                if (in->poke_eye) b->valence -= 0.03f;
                enter(b, BEH_POKED, now_ms);
            }
            else if (in->audio.active && in->audio.speech && !in->user_interacting) {
                b->speech_last_ms = now_ms;
                roll_listen_face(b, now_ms);
                enter(b, BEH_LISTENING, now_ms);
            }
            else if (in->audio.active && music_detected(&in->audio)) {
                b->sniffing = false;
                b->music_quiet_since_ms = now_ms;
                /* roll the reaction against his mood */
                const float r = frand(b);
                if (r < 0.12f + 0.25f * (1.f - b->energy)) {
                    enter(b, BEH_UNIMPRESSED, now_ms);
                } else {
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
    /* on the charger the microphones simply stay on: nothing to save, and he hears people talking */
    out->want_mic = b->sniffing || b->state == BEH_MUSIC || b->state == BEH_LISTENING ||
                    (in->usb && in->mic_available && (b->state == BEH_IDLE || b->state == BEH_UNIMPRESSED));

    /* --- outputs --- */
    switch (b->state) {
    case BEH_DIZZY:       out->override_anim = ANIM_DIZZY; break;
    case BEH_KNOCKED_OUT: out->knocked_out = true; out->override_anim = ANIM_SLEEPING; break;
    case BEH_GROGGY:      out->override_anim = ANIM_SLEEPY; break;
    case BEH_FACE_DOWN:   out->override_anim = ANIM_SLEEPING; out->zz = true; break;
    case BEH_WAKING:      out->override_anim = ANIM_SURPRISED; break;
    case BEH_MUSIC:       out->override_anim = ANIM_DANCE; break;
    case BEH_UNIMPRESSED: out->override_anim = ANIM_ANNOYED; break;
    case BEH_LISTENING:   out->override_anim = b->listen_anim; break;
    case BEH_POKED:       out->override_anim = b->poked_eye ? b->idle_anim : b->poke_anim; break;   /* an eye poke keeps the face */
    case BEH_PETTED:      out->override_anim = ANIM_LOVE; break;
    case BEH_IDLE:
        /* idle life: the face of the moment, re-rolled every 15-55 s */
        if ((int32_t)(now_ms - b->idle_roll_ms) > 0) roll_idle_face(b, now_ms);
        out->override_anim = b->idle_anim;
        break;
    case BEH_CARRIED:     out->override_anim = ANIM_SQUINT; break;
    case BEH_STARTLED:    out->override_anim = ANIM_SURPRISED; break;
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
    /* the voice: the eyes drift toward the talker along the mic axis (+ = the lanyard end = the top) */
    /* the direction cue is small and noisy: take its side (with a dead band) and lean all the way */
    float want_voice = 0.f;
    if (b->state == BEH_LISTENING && in->audio.active) want_voice = in->audio.dir > 0.12f ? 1.f : in->audio.dir < -0.12f ? -1.f : 0.f;
    b->voice_dir += (want_voice - b->voice_dir) * 0.04f;
    if (in->have_accel && (b->state == BEH_IDLE || b->state == BEH_MUSIC || b->state == BEH_WAKING || b->state == BEH_LISTENING ||
                           b->state == BEH_STARTLED || b->state == BEH_CARRIED || b->state == BEH_POKED || b->state == BEH_PETTED)) {
        float bx = -b->gx + (b->state == BEH_STARTLED ? b->spike_side * 0.9f : 0.f), by = -b->gy;
        if (bx > 1.f) bx = 1.f;
        if (bx < -1.f) bx = -1.f;
        if (by > 1.f) by = 1.f;
        if (by < -1.f) by = -1.f;
        /*
         * With the face following gravity, the residual gaze is what is left after
         * the turn. Use the angle the face is heading to, not the eased one: while
         * the face is still turning the eyes would otherwise chase a residual that
         * vanishes at the end of the turn, sliding one way and back.
         */
        const float turn_deg = CONFIG_EYES_GRAVITY_FACE ? b->face_target : out->face_angle_deg;
        if (turn_deg != 0.f) {
            const float r = -turn_deg * 0.01745329f;
            const float c = cosf(r), sn = sinf(r);
            const float rx = bx * c - by * sn, ry = bx * sn + by * c;
            bx = rx; by = ry;
        }
        /* slight handling: a small wobble proportional to the motion, quiet while the face is mid-turn */
        float wob = b->shake > 0.03f ? (b->shake - 0.03f) * 40.f : 0.f;
        if (wob > 6.f) wob = 6.f;
        float turning = fabsf(b->face_target - b->face_angle);
        while (turning > 180.f) turning -= 360.f;
        turning = fabsf(turning);
        if (turning > 2.f) wob *= turning >= 30.f ? 0.f : 1.f - turning / 30.f;
        const float ph = (float)(now_ms % 1000) * 0.0062831853f * 6.f;    /* 6 Hz */
        const float wx = sinf(ph) * wob, wy = cosf(ph * 1.3f) * wob * 0.6f;
        /* a poked eye shuts for the reaction, the other stays open */
        if (b->state == BEH_POKED && b->poked_eye) {
            const int e = b->poked_eye - 1;
            const float in_ms = (float)(now_ms - b->state_since_ms);
            /* shut in 150 ms, hold, reopen over half a second with an ease */
            float shut = in_ms < 150.f ? in_ms / 150.f : in_ms < 800.f ? 1.f : in_ms < 1350.f ? (1350.f - in_ms) / 550.f : 0.f;
            shut = shut * shut * (3.f - 2.f * shut);
            out->env[e].lid_top = (int32_t)(shut * 0.75f * 65536.f);
        }
        /* the voice's lean is in screen terms: + = the lanyard end = up */
        const float lean = -b->voice_dir * VOICE_LEAN_PX;
        for (int e = 0; e < 2; e++) {
            out->env[e].dx = (int32_t)((bx * GAZE_PX_X + wx) * 65536.f);
            out->env[e].dy = (int32_t)((by * GAZE_PX_Y + wy + lean) * 65536.f);
        }
    }
}
