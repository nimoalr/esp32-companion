# Personality engine: proposal

Status: proposal for discussion, nothing implemented. Builds on
[sound.md](sound.md) (the voice) and [animation.md](animation.md) (the face).
The constraint that shapes everything: a 240 MHz dual core with the render
task on core 1 and the audio task on core 0, and a face that must stay at
60 fps whatever the character is thinking. Every signal below has a cost
column for that reason.

## 1. What he can sense

The board has no camera, no light sensor and no range sensor. It has two
microphones, a 6-axis IMU, a touch panel, a fuel gauge and a real-time clock
chip that nothing sets. That is enough for a surprising amount of context if
each signal is squeezed properly.

| Source | Raw signal | Derived context | Cost |
| --- | --- | --- | --- |
| Microphones | stereo 16 kHz, 256-sample frames (16 ms) | sound presence, music with tempo (done); **speech**; **direction along the mic axis**; loudness trend (room getting lively / quiet); claps and knocks; whistles | ~0.3 ms per frame today; speech + direction add ~0.2 ms |
| IMU (100 Hz) | acceleration, no gyro used yet | orientation and gravity face (done); shake, face-down (done); **pick-up / put-down**; **carried** (walking rhythm 1.5-2.5 Hz); **tap on the body**; **hanging on the lanyard** (upright + swinging) | negligible, already sampled |
| Touch | taps, swipes, hold, finger position (done) | **stroking / petting** (repeated strokes across the eyes); poke vs rest; where on the face he is touched | negligible |
| Fuel gauge / PMIC | percent, charging, USB | tiredness as the battery drains; relief when plugged; "asleep on the charger" | already read |
| RTC (PCF85063) | wall-clock time once set | time of day: sleepy evenings, bright mornings, quiet at night | one I2C read per minute |
| Clock | uptime | how long since the last interaction, since the last sound, since he woke | free |

### Speech detection

Music was separated from speech by tempo. Speech has its own signature that
is cheap to compute from the band energies the audio task already has:

* energy concentrated in the 300 Hz to 3 kHz mid band, with a **syllable
  rhythm**: the mid-band envelope is modulated at 3 to 8 Hz;
* no steady tempo (the beat tracker's confidence stays low);
* bursts of 0.3 to 3 s separated by pauses.

Plan: track the mid-band envelope, measure its modulation depth in the 3-8 Hz
range with a two-pole band-pass on the envelope (a few multiplies per frame),
and declare "someone is talking" when depth and presence hold for 0.6 s. No
FFT beyond the one we run.

### Where the voice comes from

The two microphones sit at the bottom-left, next to the USB port, and at the
top-left, next to the lanyard holes. Two microphones give one axis, not a
bearing: the time difference between them says how far the sound is towards
the USB end or the lanyard end of the device, with left/right ambiguous.
The spacing is roughly 40 mm, which at 16 kHz is about two samples of delay
end to end, so the estimate is coarse but real: cross-correlate the two
channels over lags of -3..+3 samples, take the peak with parabolic
interpolation, and average it with the level difference between the mics
(which the shell makes the more reliable of the two cues), each mapped
through the wizard's calibration; smooth over a second. Cost: 7 lags x 256
samples per frame, well under 0.1 ms. Done, see `micdir.c` and the wizard.

What it gives the face: on a desk, lying flat, the axis is "the USB edge
versus the lanyard edge", so when someone talks from one side of the table
the eyes drift that way. Held upright the axis is up/down, which is nearly
always "the person's mouth is above me", so the gaze lifts. Combined with the
orientation from the IMU the same number means the right thing in both
cases. This is worth doing; it is not a bearing and the document should not
promise one.

### Handling gestures from the IMU

* **Pick-up**: a vertical acceleration pulse followed by sustained motion,
  from a period of stillness. A small "oh".
* **Put-down**: a short impact followed by stillness. Settles, looks around.
* **Carried**: 1.5-2.5 Hz periodic vertical motion for more than a few
  seconds. Content, eyes half-closed, occasional glance.
* **Body tap**: a sharp spike with no touch event. Startled, looks at the side
  of the tap if the sign of the lateral acceleration says which.
* **Hanging**: upright for a long time with gentle swinging. Bored, dozing.

## 2. The engine

Today the behaviour module is a list of reactions (dizzy, knocked out,
face-down, music, unimpressed) with one drive, `energy`. Vector's mood
manager is the model to borrow: a few emotion dimensions that events push and
that decay toward a baseline, and behaviours chosen by weighted priority with
cooldowns so nothing repeats on a schedule.

### Drives (slow, hours) and moods (fast, seconds to minutes)

| Drive | Rises with | Falls with | Shows as |
| --- | --- | --- | --- |
| Energy | charge, rest, mornings | uptime, activity, evenings, low battery | brightness, blink rate, how much he moves |
| Social | voices, touch, being carried | hours alone | how eagerly he looks at people, chattiness |
| Curiosity | new sounds, being moved to a new place, being picked up | repetition | look-around frequency, head tilts |
| Comfort | resting on the charger, gentle handling, stroking | shaking, drops, loud noise | calm vs jittery motion, purring |

Moods are derived from the drives plus recent events: content, playful,
curious, sleepy, startled, grumpy, lonely, loving. Each expression already has
a tint and an idle rate; the mood picks which idle behaviours are on the menu
and with what weights. Habituation matters: the same stimulus repeated within
a few minutes gets a smaller reaction each time, then a bored one.

### Arbitration

Three layers, highest wins, lower layers resume when it ends:

1. **Reflexes** (immediate, interrupt anything): shake, drop, knock-out,
   face-down, loud bang, being picked up.
2. **Engagements** (seconds to minutes, started by context): someone talking
   (look at them, react to their rhythm), music (dance), being stroked
   (purr, melt), being carried (settle), the setup UI.
3. **Idle life** (the default): a weighted random choice among behaviours
   allowed by the mood, each with a cooldown and a duration: look around,
   micro-expressions, a sigh, a stretch, a glance at the low side, dozing
   off, a daydream (slow colour drift), practising a dance move, humming.

Every transition eases through the existing expression system; nothing pops.

### Attention

One "focus" at a time with a strength that decays: a finger resting on the
screen (done), the side a voice comes from, the low side of the device, a
body tap, nothing. The eyes look at the focus; the mood decides how eagerly
(a sleepy character glances, a curious one stares).

### Memory, kept tiny

Persisted, a few bytes: how much he has been handled today, how many times
he was left alone for hours, the last time he heard music, whether he was
shaken recently. Enough for "glad to see you" after a long quiet spell and
"sulking" after rough handling, without a history.

## 3. The voice

[sound.md](sound.md) still stands: procedural, two oscillators, pitch
gestures per trigger, mood-scaled volume and chattiness, no samples, no
words. Now that the speaker codec is clocked (the microphone fix), it is a
driver addition plus a synthesiser in the audio task, generated in 10 ms
blocks at well under 1 % of a core.

What the personality adds to that proposal:

* the voice register is a personality trait, not a setting: chosen once at
  first boot from a small set and kept;
* utterances are rarer than expressions: a chattiness budget per hour that
  the social drive spends, so he does not chirp at every blink;
* the voice answers speech: when someone talks nearby he may answer with a
  short "hm?" or a two-note acknowledgement, more so when the social drive is
  high, and he goes quiet when the talking is continuous (a conversation is
  not about him);
* the microphones pause while he speaks so his own voice never counts as a
  beat or a voice.

First step, as before: render the vocabulary to WAV on the host and listen
before any of it goes into firmware.

## 4. New visuals

* **Spectrum eyes** for the dance (done): the eye shape stays the mask, the
  fill becomes eight bars per eye driven by sixteen log-spaced FFT bands, low
  on the left eye and high on the right, rising from the bottom of the eye's
  box; a fill mode in the rasteriser (a per-column top with an anti-aliased
  edge, through the hot spot's lightness LUT), pixel-identical edges. See
  `docs/images/spectrum_eyes.png`.
* **Hearts** for the loving mood, and for petting (strokes across the forehead already put him in the love face with a purr; the heart eyes go there): a new shape kind. A heart is two discs and
  a wedge, so its rows are one or two spans, and it beats by scaling with a
  60-100 bpm envelope. The eye system already animates size, so the beat is a
  modulator like the dance's.
* **Disco ball** for high-energy dance: a sprite of a dotted sphere with a
  slow rotation, drawn as an accessory above the eyes, plus light spots that
  sweep across the eye fill. Cheaper than it sounds if the sphere is a
  pre-rendered 2-bit mask rotated by table lookup.
* **Colour and tint** already respond to mood; the loving mood pinks, the
  grumpy one dims and reddens.

## 5. Performance rules

* All audio features on core 0 in the audio task, budgeted at 1.5 ms per
  16 ms frame including the voice synthesiser. Today: 0.3 ms.
* The engine ticks at 60 Hz on the render task in a fixed few microseconds:
  no allocation, no string handling, tables only.
* New visuals go through the same dirty-rect and frame-buffer path; anything
  that would need a full-screen repaint per frame is out.
* Every reaction eases through the expression system, and every new shape
  goes through the host equivalence and benchmark harnesses before the board.

## 6. Order of work

1. Speaker bring-up and the procedural voice, host-rendered first.
2. Speech detection and voice direction; log them for a day of normal use.
3. IMU handling gestures.
4. The engine: drives, moods, arbitration, attention, tiny memory. Replace
   the ad-hoc behaviour states with it, keeping their reactions.
5. Spectrum eyes and hearts; then the disco ball.
6. A week of tuning from the log.

## Open questions

* Microphone positions: confirmed with the wizard on 2026-09-06. MIC1 (I2S
  slot 0, the left channel) is the microphone next to the USB port, MIC2 the
  one next to the lanyard holes. The shell is one shared cavity fed by the mic
  holes, two open lanyard holes and the USB cutout, so the arrival-time
  difference alone was a coin toss per clap until the mics were sealed to
  their holes with foam tape; even then the ends differ by about a sample and
  2-4 dB. The direction estimate therefore fuses timing and level, and is a
  coarse "USB end or lanyard end" cue, not a bearing. Details in
  [hardware.md](hardware.md).
* Time of day: setting the RTC needs a way in (the setup UI, or a one-off
  from a phone over Wi-Fi). Without it, "evening" can only be inferred from
  how long he has been awake.
* How much should he talk? Vector was chatty; this is a desk companion that
  sits closer.
