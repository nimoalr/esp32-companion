#include "persona.h"

#include <string.h>

#define MIN_GAP_MS 4000                 /* between any two utterances, reflexes excepted */
static const uint32_t k_idle_ms[4] = { 0, 300000, 120000, 20000 };   /* by chattiness; 0 = never */

static uint32_t rnd(persona_t *p)
{
    p->rng = p->rng * 1664525u + 1013904223u;
    return p->rng >> 8;
}

static bool chance(persona_t *p, int pct)
{
    return (int)(rnd(p) % 100u) < pct;
}

static int pick(persona_t *p, const int *list, int n)
{
    return list[rnd(p) % (uint32_t)n];
}

void persona_init(persona_t *p, uint32_t now_ms, uint32_t seed)
{
    memset(p, 0, sizeof *p);
    p->rng = seed ? seed : 7;
    p->last_say_ms = now_ms;
    p->next_idle_ms = now_ms + 20000;
}

static void schedule_idle(persona_t *p, const persona_in_t *in, uint32_t now_ms)
{
    const uint32_t base = k_idle_ms[in->chattiness < 0 ? 0 : in->chattiness > 3 ? 3 : in->chattiness];
    if (!base) { p->next_idle_ms = 0; return; }
    /* 0.6..1.4 of the base, a tired character waits longer */
    const float jitter = 0.6f + 0.8f * (float)(rnd(p) % 1000u) / 1000.f;
    const float tired = 1.5f - 0.5f * in->energy;
    p->next_idle_ms = now_ms + (uint32_t)((float)base * jitter * tired);
}

static void say_gesture(persona_say_t *o, voice_id_t id, float level, bool interrupt)
{
    o->kind = SAY_GESTURE; o->id = (int)id; o->level = level; o->interrupt = interrupt;
}

static void say_word(persona_say_t *o, int clip, float level, bool interrupt)
{
    o->kind = SAY_WORD; o->id = clip; o->level = level; o->interrupt = interrupt;
}

/* the chirp or word that goes with an expression; -1 = nothing */
static void react_to_anim(persona_t *p, anim_id_t a, float level, persona_say_t *o)
{
    switch (a) {
    case ANIM_HAPPY:     say_gesture(o, VOICE_HAPPY, level, false); break;
    case ANIM_SAD:       say_gesture(o, VOICE_SAD, level, false); break;
    case ANIM_ANGRY:     say_gesture(o, VOICE_ANGRY, level, false); break;
    case ANIM_SURPRISED: say_gesture(o, VOICE_SURPRISED, level, false); break;
    case ANIM_SLEEPY:    say_gesture(o, VOICE_YAWN, level, false); break;
    case ANIM_CURIOUS:
    case ANIM_THINKING:
    case ANIM_LOOK_AROUND: say_gesture(o, VOICE_HM, level, false); break;
    case ANIM_WINK:      say_word(o, CLIP_HI_THERE, level, false); break;
    case ANIM_CONFUSED:  say_gesture(o, VOICE_CONFUSED, level, false); break;
    case ANIM_LOVE:      say_word(o, chance(p, 50) ? CLIP_OOH_LA_LA : CLIP_YUMMY, level, false); break;
    case ANIM_DIZZY:     say_gesture(o, VOICE_DIZZY, level, false); break;
    case ANIM_LAUGHING:  say_gesture(o, VOICE_LAUGH, level, false); break;
    case ANIM_SCARED:    say_gesture(o, VOICE_SCARED, level, false); break;
    case ANIM_SKEPTICAL: say_word(o, chance(p, 50) ? CLIP_OH_REALLY : CLIP_AS_IF, level, false); break;
    case ANIM_BORED:     say_word(o, chance(p, 50) ? CLIP_BORING : CLIP_I_AM_BORED, level, false); break;
    case ANIM_EXCITED:   say_word(o, chance(p, 50) ? CLIP_HOORAY : CLIP_BRAVO, level, false); break;
    case ANIM_SHY:       say_word(o, CLIP_OH_PLEASE, level, false); break;
    case ANIM_ANNOYED:   say_gesture(o, VOICE_ANNOYED, level, false); break;
    case ANIM_SQUINT:    say_word(o, CLIP_I_AM_WATCHING_YOU, level, false); break;
    case ANIM_NEUTRAL:   say_word(o, CLIP_OKAY, level, false); break;
    case ANIM_SMUG:      say_word(o, CLIP_NICE_TRY, level, false); break;
    case ANIM_SUSPICIOUS: say_word(o, CLIP_I_AM_WATCHING_YOU, level, false); break;
    case ANIM_DETERMINED: say_word(o, CLIP_COME_ON, level, false); break;
    case ANIM_PLEADING:  say_word(o, CLIP_OH_PLEASE, level, false); break;
    case ANIM_MISCHIEVOUS: say_word(o, CLIP_PEEKABOO, level, false); break;
    case ANIM_EMBARRASSED: say_word(o, CLIP_OOPSIE, level, false); break;
    case ANIM_RELIEVED:  say_word(o, CLIP_OKAY, level, false); break;
    case ANIM_DOUBLE_TAKE: say_word(o, CLIP_SERIOUSLY, level, false); break;
    case ANIM_HEARTS:    say_word(o, CLIP_OOH_LA_LA, level, false); break;
    case ANIM_HEARTBREAK: say_word(o, CLIP_OH_NO, level, false); break;
    case ANIM_HIGH_ROLLER: say_word(o, CLIP_BINGO, level, false); break;
    case ANIM_NOD:       say_word(o, CLIP_OKAY, level, false); break;
    case ANIM_PEEKABOO:  say_word(o, CLIP_PEEKABOO, level, false); break;
    case ANIM_BOOP:      say_word(o, CLIP_OOPSIE, level, false); break;
    default: break;
    }
}

/* records a tap; true when three came within 2.5 s */
static bool poke_flurry(persona_t *p, uint32_t now_ms)
{
    if (p->taps_n < 4) p->taps_ms[p->taps_n++] = now_ms;
    else { memmove(p->taps_ms, p->taps_ms + 1, sizeof(uint32_t) * 3); p->taps_ms[3] = now_ms; }
    int recent = 0;
    for (int i = 0; i < p->taps_n; i++) if ((int32_t)(now_ms - p->taps_ms[i]) < 2500) recent++;
    return recent >= 3;
}

static bool is_recent(const persona_t *p, const persona_say_t *o)
{
    for (int i = 0; i < 2; i++) if (p->recent_kind[i] == (int)o->kind && p->recent_id[i] == o->id) return true;
    return false;
}

void persona_tick(persona_t *p, const persona_in_t *in, uint32_t now_ms, persona_say_t *out)
{
    memset(out, 0, sizeof *out);
    if (!p->primed) {
        p->prev = *in;
        p->primed = true;
        schedule_idle(p, in, now_ms);
        return;
    }
    const persona_in_t *pv = &p->prev;
    const float level = 0.6f + 0.4f * in->energy;
    const bool gap_ok = (int32_t)(now_ms - p->last_say_ms) > MIN_GAP_MS;
    const bool free = !in->speaking && gap_ok && !in->in_ui;

    /* Track touch continuously, even when a higher-priority reaction speaks.
     * Carrying or releasing the finger restarts the deliberate hold duration. */
    const bool gentle_touch = in->finger && !in->handling && in->power == 0 && !in->in_ui && !in->dancing;
    if (!gentle_touch) { p->finger_since_ms = 0; p->purred = false; }
    else if (!p->finger_since_ms) p->finger_since_ms = now_ms;

    /* power transitions: reflexes that may interrupt */
    if (in->power != pv->power) {
        if (pv->power == 0 && in->power == 1 && chance(p, 70)) say_gesture(out, VOICE_YAWN, 0.7f, false);
        else if (in->power == 2) say_word(out, CLIP_GOOD_NIGHT, 0.7f, true);
        else if (pv->power == 2 && in->power == 0) {
            static const int w[] = { CLIP_HELLO, CLIP_HI_THERE, CLIP_GOOD_MORNING };
            if (chance(p, 60)) say_word(out, pick(p, w, 3), 0.8f, true);
            else say_gesture(out, VOICE_WAKE, 0.8f, true);
        } else if (pv->power == 1 && in->power == 0 && chance(p, 50)) say_gesture(out, VOICE_HM, 0.7f, false);
    }
    /* during the dance a touch is a move: cheer, never complain */
    else if (in->dancing && (in->tap_count != pv->tap_count || in->stroke_count != pv->stroke_count) && !in->in_ui) {
        if (free && chance(p, 30)) {
            static const int w[] = { CLIP_HOORAY, CLIP_COME_ON, CLIP_BRAVO, CLIP_OOH_LA_LA, CLIP_YUMMY };
            if (chance(p, 50)) say_gesture(out, chance(p, 50) ? VOICE_HAPPY : VOICE_LAUGH, 1.f, false);
            else say_word(out, pick(p, w, 5), 1.f, false);
        }
    }
    /* a flurry of pokes gets a complaint before anything else */
    else if (in->tap_count != pv->tap_count && in->power == 0 && !in->in_ui && poke_flurry(p, now_ms) && chance(p, 60)) {
        static const int w[] = { CLIP_DO_NOT_TOUCH_ME, CLIP_EXCUSE_ME, CLIP_HOW_RUDE, CLIP_NOPE };
        say_word(out, pick(p, w, 4), 1.f, true);
        out->feel = -0.1f;
        p->taps_n = 0;
    }
    /* the behaviour's reactions */
    else if (in->beh != pv->beh && !in->in_ui) {
        switch (in->beh) {
        case BEH_DIZZY: {
            /* shaken: protest, then rudeness, then worse; the count forgets after two minutes */
            int n = 0;
            for (int i = 0; i < p->dizzy_n; i++) if ((int32_t)(now_ms - p->dizzy_ms[i]) < 120000) n++;
            if (p->dizzy_n < 4) p->dizzy_ms[p->dizzy_n++] = now_ms;
            else { memmove(p->dizzy_ms, p->dizzy_ms + 1, sizeof(uint32_t) * 3); p->dizzy_ms[3] = now_ms; }
            out->feel = -0.2f;
            if (n == 0) say_gesture(out, VOICE_PROTEST, 1.f, true);
            else if (n == 1) say_word(out, chance(p, 50) ? CLIP_HOW_RUDE : CLIP_DO_NOT_TOUCH_ME, 1.f, true);
            else {
                static const int w[] = { CLIP_FUCK_YOU, CLIP_LEAVE_ME_ALONE, CLIP_SHUT_UP, CLIP_YOU_IDIOT };
                say_word(out, pick(p, w, 4), 1.f, true);
            }
            break;
        }
        case BEH_KNOCKED_OUT: say_gesture(out, VOICE_KO, 1.f, true); break;
        case BEH_GROGGY:      say_word(out, CLIP_UH_OH, 0.8f, false); break;
        case BEH_FACE_DOWN:   say_word(out, chance(p, 50) ? CLIP_EXCUSE_ME : CLIP_HELLO, 0.8f, false); break;
        case BEH_WAKING:      say_gesture(out, VOICE_CONFUSED, 0.8f, false); break;
        case BEH_MUSIC: {
            static const int w[] = { CLIP_OOH_LA_LA, CLIP_COME_ON, CLIP_HOORAY };
            if (chance(p, 60)) say_word(out, pick(p, w, 3), 1.f, false);
            else say_gesture(out, VOICE_HAPPY, 1.f, false);
            break;
        }
        case BEH_UNIMPRESSED: say_word(out, chance(p, 50) ? CLIP_BORING : CLIP_MEH, 0.9f, false); break;
        case BEH_CARRIED:     if (free && chance(p, 30)) say_gesture(out, VOICE_HM, level, false); break;
        case BEH_POKED:
            if (in->valence < -0.3f && chance(p, 50)) {
                static const int w[] = { CLIP_DO_NOT_TOUCH_ME, CLIP_GO_AWAY, CLIP_LEAVE_ME_ALONE, CLIP_NOPE, CLIP_BUZZ_OFF };
                say_word(out, pick(p, w, 5), 1.f, false);
            } else if (chance(p, 45)) react_to_anim(p, in->anim, level, out);
            break;
        case BEH_PETTED:
            out->feel = 0.05f;
            if (chance(p, 60)) say_gesture(out, VOICE_PURR, 0.8f, false);
            else say_word(out, chance(p, 50) ? CLIP_OOH_LA_LA : CLIP_YUMMY, level, false);
            break;
        case BEH_STARTLED: {
            static const int w[] = { CLIP_EXCUSE_ME, CLIP_HOW_RUDE, CLIP_DO_NOT_TOUCH_ME, CLIP_NOPE };
            out->feel = -0.05f;
            if (chance(p, 50)) say_gesture(out, VOICE_SURPRISED, 1.f, true);
            else say_word(out, pick(p, w, 4), 1.f, true);
            break;
        }
        default: break;
        }
    }
    /* handling */
    else if (in->event != BEH_EV_NONE && in->power <= 1 && !in->in_ui) {
        switch (in->event) {
        case BEH_EV_PICKED_UP: {
            static const int w[] = { CLIP_HI_THERE, CLIP_HELLO, CLIP_EXCUSE_ME, CLIP_OKAY };
            if (chance(p, 55)) say_gesture(out, VOICE_OH, 0.9f, false);
            else say_word(out, pick(p, w, 4), 0.9f, false);
            break;
        }
        case BEH_EV_PUT_DOWN:
            if (chance(p, 40)) say_gesture(out, VOICE_HM, level, false);
            else if (chance(p, 50)) say_word(out, chance(p, 50) ? CLIP_OKAY : CLIP_BYE_BYE, level, false);
            break;
        default: break;     /* a body tap speaks through the startled state */
        }
    }
    /* the charger: every plug and unplug gets a line, and the line depends on how hungry he is */
    else if (in->usb != pv->usb && in->power <= 1 && !in->in_ui) {
        const int pct = in->batt_pct < 0 ? 50 : in->batt_pct;
        const float lv = in->power == 0 ? 0.9f : 0.6f;
        if (in->usb) {
            if (pct < 20) {
                static const int w[] = { CLIP_YUMMY, CLIP_THANK_YOU, CLIP_HOORAY, CLIP_YUMMY };
                say_word(out, pick(p, w, 4), lv, false);
            } else if (pct < 80) {
                static const int w[] = { CLIP_OKAY, CLIP_THANK_YOU, CLIP_AHA, CLIP_YUMMY };
                if (chance(p, 30)) say_gesture(out, VOICE_OH, lv, false);
                else say_word(out, pick(p, w, 4), lv, false);
            } else {
                static const int w[] = { CLIP_WHATEVER, CLIP_MEH, CLIP_OH_PLEASE, CLIP_REALLY };
                if (chance(p, 30)) say_gesture(out, VOICE_ANNOYED, lv, false);
                else say_word(out, pick(p, w, 4), lv, false);
            }
        } else {
            if (pct < 15) {
                static const int w[] = { CLIP_OH_NO, CLIP_FEED_ME, CLIP_HOW_RUDE, CLIP_SERIOUSLY };
                say_word(out, pick(p, w, 4), lv, false);
            } else if (pct < 40) {
                static const int w[] = { CLIP_UH_OH, CLIP_SERIOUSLY, CLIP_EXCUSE_ME, CLIP_OH_NO };
                say_word(out, pick(p, w, 4), lv, false);
            } else if (pct < 80) {
                static const int w[] = { CLIP_OKAY, CLIP_REALLY, CLIP_HI_THERE };
                if (chance(p, 40)) say_gesture(out, VOICE_HM, lv, false);
                else say_word(out, pick(p, w, 3), lv, false);
            } else {
                static const int w[] = { CLIP_BYE_BYE, CLIP_OKAY, CLIP_HOORAY, CLIP_BRAVO };
                if (chance(p, 30)) say_gesture(out, VOICE_HAPPY, lv, false);
                else say_word(out, pick(p, w, 4), lv, false);
            }
        }
    }
    /* someone talking: a short answer when a conversation starts, then quiet (it is not about him) */
    else if (in->speech && in->power == 0 && !in->in_ui) {
        if ((int32_t)(now_ms - p->talk_last_ms) > 60000) { p->talk_since_ms = now_ms; p->answered = false; }
        p->talk_last_ms = now_ms;
        if (!p->answered && (int32_t)(now_ms - p->talk_since_ms) > 1200 && free) {
            p->answered = true;
            const int r = (int)(rnd(p) % 100u);
            if (r < 45) say_gesture(out, VOICE_HM, level, false);
            else if (r < 65) say_word(out, CLIP_REALLY, level, false);
            else if (r < 80) say_word(out, CLIP_AHA, level, false);
            else if (r < 90) say_word(out, CLIP_OH_REALLY, level, false);
            else say_word(out, CLIP_EXCUSE_ME, level, false);
        }
    }
    /* a finger resting: a purr, once per touch (not during the dance: nothing touches the dance) */
    else if (gentle_touch) {
        if (!p->purred && (int32_t)(now_ms - p->finger_since_ms) > 3500 && free) {
            p->purred = true;
            say_gesture(out, VOICE_PURR, level, false);
        }
    }
    /* battery */
    else if (!in->usb && in->batt_pct >= 0 && in->batt_pct < 15 && in->power == 0 && free &&
             (!p->low_batt_ms || (int32_t)(now_ms - p->low_batt_ms) > 600000)) {
        p->low_batt_ms = now_ms;
        say_word(out, CLIP_FEED_ME, 0.9f, false);
    }
    /* nothing happening: talk to himself, at the chattiness's pace */
    else if (free && in->power == 0 && in->beh == BEH_IDLE && in->anim != ANIM_DANCE && in->anim != ANIM_SLEEPING &&
             p->next_idle_ms && (int32_t)(now_ms - p->next_idle_ms) > 0) {
        for (int attempt = 0; attempt < 4; attempt++) {
            memset(out, 0, sizeof *out);
            const int r = (int)(rnd(p) % 100u);
            if (r < 50) {
                out->kind = SAY_BABBLE; out->level = level; out->energy = in->energy;
            } else if (r < 75) {
                react_to_anim(p, in->anim, level, out);
                if (out->kind == SAY_NONE) say_gesture(out, VOICE_HM, level, false);
            } else {
                static const int nice[] = { CLIP_HELLO, CLIP_PEEKABOO, CLIP_AHA, CLIP_HI_THERE, CLIP_OOH_LA_LA, CLIP_YUMMY, CLIP_BRAVO, CLIP_HOORAY };
                static const int flat[] = { CLIP_HELLO, CLIP_I_AM_BORED, CLIP_MEH, CLIP_WHATEVER_HUMAN, CLIP_I_AM_WATCHING_YOU,
                                            CLIP_PEEKABOO, CLIP_AHA, CLIP_HI_THERE };
                static const int sour[] = { CLIP_BORING, CLIP_MEH, CLIP_WHATEVER, CLIP_AS_IF, CLIP_YOU_WISH, CLIP_NERD, CLIP_LOSER, CLIP_I_AM_BORED };
                int id = in->valence > 0.35f ? pick(p, nice, 8) : in->valence < -0.35f ? pick(p, sour, 8) : pick(p, flat, 8);
                if (in->batt_pct >= 0 && in->batt_pct < 40 && chance(p, 30)) id = CLIP_FEED_ME;
                say_word(out, id, level, false);
            }
            if (out->kind == SAY_BABBLE || !is_recent(p, out)) break;    /* never the same as the last two */
        }
        schedule_idle(p, in, now_ms);
    }

    if (out->kind != SAY_NONE) {
        /* a reaction that would repeat one of the last two becomes a wordless remark instead */
        if (out->kind != SAY_BABBLE && !out->interrupt && is_recent(p, out)) {
            const float lv = out->level;
            memset(out, 0, sizeof *out);
            out->kind = SAY_BABBLE; out->level = lv; out->energy = in->energy;
        }
        p->recent_kind[1] = p->recent_kind[0]; p->recent_id[1] = p->recent_id[0];
        p->recent_kind[0] = (int)out->kind; p->recent_id[0] = out->id;
        p->last_say_ms = now_ms;
        if (p->next_idle_ms) schedule_idle(p, in, now_ms);
    }
    if (in->chattiness != pv->chattiness) schedule_idle(p, in, now_ms);
    p->prev = *in;
}
