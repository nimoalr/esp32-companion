# Sound: proposal

Status: proposal, nothing implemented. Written after reading how Vector does
it, so we can decide what to borrow before writing firmware.

## How Vector makes sound

* **Sample-based, through middleware.** Vector runs Audiokinetic Wwise. Every
  sound is a recorded or designed clip in a sound bank (Ben Gabaldon's sound
  design). The banks are proprietary and are not in the open-source repo;
  only the event names survive in the code.
* **Events, not files.** Code posts an event such as
  `Play__Robot_Vic_Sfx__Scrn_Procedural_Blink`. Wwise maps it to a random
  container that picks one of several variants, so a reaction never sounds
  identical twice.
* **Mood shapes the sound.** The emotion engine feeds parameters into Wwise:
  `Robot_Vic_Stimulation`, `Happy`, `Confident`, `Social`, `Held_Trust`,
  `Purr_Level`. Volume and the probability of a sound fall with stimulation,
  so a robot left alone is quiet and one being handled is lively. Motor
  speed and head position drive parameters too, so movements grunt.
* **Two trigger sources.** Animation files carry an audio track with
  keyframes; behaviours post events directly (including the wake-word
  earcons). The procedural face posts three of its own: blink, eye shift
  (with the shift duration as a parameter) and squint. His eyes tick when
  they move.
* **Speech** is Acapela text to speech, a licensed human voice, pushed
  through the Krotos Dehumaniser vocoder for the robotic timbre. Speech is an
  animation with only an audio track.

Sources: Vector `animProcess/.../audioLayerManager.cpp` (eye dart and squint
events), the Vector technical reference chapters "Audio production" and
"Text to speech", and the "Audio events" section of the behaviour chapter.

## What the board has

ES8311 DAC into an NS4150B class-D amplifier (enable on GPIO46) and a small
speaker. The ES8311 shares the I2S bus with the ES7210 microphones (MCLK 42,
BCLK 9, LRCK 45, speaker data on GPIO8, microphone data on GPIO10), and the
`esp_codec_dev` component already in the build supports it. Playback is a
driver addition, not new hardware.

## Deliberately left out

* **Speech.** Needs a licensed voice, megabytes of data and a vocoder, and
  sounds cheap on a two centimetre speaker. The charm of Cozmo and Vector is
  vocalising without words; words would break the character.
* **A sample library.** No sound designer, variety fixed at what ships, a
  licence per clip.
* **Middleware.** Wwise's value was a designer's workflow; we have one
  character and a few dozen events.
* **Sound on every blink or dart by default.** Vector's eye ticks work from
  across a room; in the hand they would grate. An off-by-default option at
  most.

## Proposal: a procedural voice

Generate vocalisations in real time (the R2-D2 / WALL-E school): two
oscillators, pitch glides, an amplitude envelope, vibrato, a little noise, a
one-pole low-pass. Each utterance is a short *gesture* described by a few
numbers, so the vocabulary is a table like the expressions. Every call gets a
random detune and timing jitter, so nothing repeats exactly.

Mood plugs in the way Vector's parameters do:

* energy scales volume and the chance of speaking at all;
* a fast pitch wobble follows shaking and dizziness;
* the character's pitch register is a setting (low / mid / high voice);
* a chattiness setting from off to three.

### Trigger set for a first version

| Trigger | Gesture |
| --- | --- |
| HAPPY, LAUGHING | rising two-note chirp; laughing adds staccato pulses |
| SAD | falling slide |
| SURPRISED, SCARED | quick rising whoop; scared trills |
| ANGRY, ANNOYED | low buzz |
| SLEEPY, waking | long descending yawn glide |
| LOVE, finger resting on the screen | purr that builds while the touch lasts |
| CURIOUS, CONFUSED | rising "hm?" contour |
| DIZZY | wobbly vibrato slide |
| shaken | protest chirps |
| knocked out | falling tone |
| picked up | small "oh" |
| tap | blip |
| listening for music, DROWSY, SLEEP | silence |

### Cost

16 kHz mono, 16-bit, generated in 10 ms blocks on core 0 next to the audio
capture: well under one percent of a core. Two 320-sample buffers. No flash
assets. The amplifier is enabled only while a sound plays (plus a short tail
with a level ramp against pops), so idle battery life does not change. The
microphones share the bus; listening pauses while a sound plays, which keeps
the speaker out of the beat detector.

### Alternatives considered

1. Procedural voice (this proposal).
2. A short bank of CC0 samples: better texture, fixed variety, curation and
   licence work, flash cost.
3. Hybrid: procedural voice plus a few textured samples (a thud for the
   knock-out). Possible later if a gesture needs it.

### Next step

Render the whole vocabulary to WAV files on the host, listen, reject and
tune before any of it goes into the firmware.
