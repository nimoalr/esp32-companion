# Annotated microphone corpus: first detector pass

Analysis date: 2026-09-09. Baseline: `14583a7`. This is a tuning pass, not a
held-out accuracy claim. Firmware built and host-tested; not flashed for this pass.
Raw recordings and model outputs stay outside the repository.

## What was recorded

The supplied notebook contains 9 runs / 1,619.36 seconds: eight music runs and a
60.816-second quiet-room reference. All 101,210 stereo frames have PCM, with no
sequence gaps or clipped samples. Gain is 30 dB. Music comes from laptop speakers,
mostly at the same reported listening level; placement varies. Two runs of
Schrottenhagen overlap and must not be treated as independent validation tracks.
There are no ordinary conversation, podcast/TV, or speech-over-music negatives.

Using the ESP microphones is the correct primary input for evaluating the device:
this includes the enclosure, microphone response/gain, playback speaker and room.
A clean track would be useful as an aligned reference for beats and stems, but
would not replace the microphone recording for scoring the firmware.

Stereo inspection found 98.3–99.4% mono RMS retention on the music runs. Independent
left/right replays of Ehla and Silver Lines have nearly identical dance coverage;
Angèle remains poor on either channel. This does not exclude frequency-specific
cancellation, but large overall cancellation is not the principal failure here.

The recordings also contain very little energy above 2 kHz: approximately 0.15–0.32%
on the checked Angèle, Ehla and Day Din runs, using a separate 2,048-point host FFT
and excluding frequencies below 60 Hz. This is an observation of the entire
playback/room/microphone chain, not a measured microphone frequency response.
A known-tone or sweep capture, preferably paired with a reference microphone,
would help locate that roll-off before increasing the capture sample rate.

## Failures and changes

The existing admission gate needs eight sub-80 Hz onsets, 85–185 BPM, at least
75% of recent intervals close to their median, and enough sub-bass energy.
It misses music when the useful periodic pattern is in a higher band, or when
its onset tracker locks to irregular subdivisions. It also confuses singing
with the syllable modulation used for listening.

This pass adds a second, conservative rhythm cue from the existing bass and high
FFT envelopes. It averages to 32 ms samples and looks for repeating modulation
across a 5.12-second window at periods of 320–1,504 ms. One band must correlate
strongly (>= .65), with support in the other (>= .12), a stable period and 1.5 s
of accumulated support. Context takes about seven seconds to fill. These are
correlation/support values, not calibrated music probabilities. The original
kick detector, beat count, refractory logic and rush detector remain in use;
this pass does **not** establish improved beat-versus-snare timing accuracy.

Confirmed music can interrupt listening, and music takes priority when both cues
are present. The song-end detector integrates quiet time so an isolated rustle
cannot keep restarting its entire silence deadline. The 45-second limit on
unconfirmed audible breakdowns remains bounded. Battery listening extends from
5.2 to 11 seconds when it hears sound, giving the second cue enough context;
quiet-room listening remains 5.2 seconds. USB idle listening remains continuous.

A separate continuous `dance_drive` controls percussion intensity. It uses
sub-bass weight, recent onset activity and tempo confidence, with smoothing.
Weak percussion produces a slow lateral sway and tilt; stronger percussion blends
in the existing hops/shuffles/stomps. The sway phase remains continuous through
tempo changes, and does not create beat events. Background lighting becomes
softer during gentle passages. Eye-fill opacity retains its existing full-opacity
fast path. This adds granularity **after music admission**; it cannot make an
unrecognised beatless intro start swaying by itself.

## Replay results

Both C replays use unmodified recorded PCM levels, fresh detector state and the
same initial awake/full-energy personality for each track. Original device flags
carry state between runs and therefore differ from a fresh baseline.

The union of explicit positive annotations covers **625.859 s**. Labelled music
response coverage rises from **25.7% to 56.5%** (160.906 to 353.774 s). The original
recorded firmware flags cover 29.8%. `unsure`, unlabelled, missing-audio and
conflicting ranges are excluded; overlapping positive labels are counted once.
Some positive comments request swaying rather than energetic dancing: this score
measures music-state coverage, not whether the chosen motion matches the comment.

| Explicit annotated range | Before | After |
|---|---:|---:|
| Ehla, clear beat, 15.397–147.869 s | 0% | 92.0% |
| Silver Lines, opening beat, 0.511–51.801 s | 0% | 83.7% |
| Schrottenhagen, second run, beat return, 47.668–69.609 s | 59.2% | 82.0% |
| Burnin, early beat, 0.520–34.426 s | 82.4% | 92.5% |
| Angèle, intro, 4.661–52.251 s | 0% | 0% |
| Angèle, vocal passage, 146.947–210.238 s | 0.4% | 0.4% |

The sole explicit negative range is Angèle's 10.575-second song end. False dancing
falls from 10.575 s to **3.199 s**; exit is still intentionally delayed rather than
instantaneous. The separate quiet-room run has 0% dancing before and after.
There is insufficient negative material to claim a real-world false-positive rate.

Whole-track percentages below are descriptive coverage, **not accuracy**: unlabelled
parts are not automatically positives or negatives.

| Run | Before | After |
|---|---:|---:|
| Quiet room | 0.0% | 0.0% |
| Dance With Me | 88.0% | 87.9% |
| Schrottenhagen, behind laptop | 73.2% | 73.3% |
| Schrottenhagen, in front | 74.3% | 76.2% |
| Cool — Ehla | 0.0% | 84.4% |
| Burnin | 96.6% | 98.4% |
| Silver Lines | 25.4% | 58.5% |
| Une Seule Vie — Angèle | 32.1% | 29.1% |
| Talk To You | 96.4% | 98.1% |

Angèle's lower whole-track total mainly reflects removal of dancing after the song
ends; its difficult positive ranges have essentially not improved. Beatless intros
and long chords-only passages remain failures. During an admitted Schrottenhagen
breakdown, mean drive is .08, versus .45 on the annotated beat return. Ehla's
labelled beat section averages .27 versus .08 in the outro. Those are useful
control distinctions, but require visual feedback; they are not calibrated labels.

## Local source-separation experiment

The selected interface is [Audio Separator](https://github.com/nomadkaraoke/python-audio-separator),
version 0.47.0 in a separate Python 3.12 environment. Its model choice is explicit,
so we can compare alternatives without changing the recording format or firmware.
Both models ran on the Mac, with all three 12-second excerpts kept local:

- `htdemucs.yaml`: vocals, drums, bass and other; 6 s segments, shifts 0, overlap .1.
- `model_bs_roformer_ep_937_sdr_10.5309.ckpt`: drum/bass versus the remainder.
  This particular RoFormer checkpoint is **not** a four-stem vocal separator.

| Excerpt | Demucs drums relative to mixture | RoFormer drum/bass relative to mixture |
|---|---:|---:|
| Ehla, 45–57 s | -4.3 dB | -4.1 dB |
| Angèle, 155–167 s | -52.3 dB | -15.4 dB |
| Silver Lines, 105–117 s | -54.5 dB | -37.9 dB |

These are estimated stem mean-square energies relative to mixture energy, not
probabilities or percentages of clean ground-truth stems. They need not sum to
100%. The models agree that percussion is much stronger in the Ehla excerpt,
but disagree about how much remains in Angèle. Demucs assigns that Angèle excerpt
mainly to vocals (-1.8 dB) and other (-5.9 dB); Silver Lines goes almost entirely
to other. This supports exploring musical continuity beyond kick presence.
Model leakage, microphone coloration and the missing high-frequency content limit
what can be inferred. Upsampling for the models does not recover missing audio.

Demucs took 10.8 s on its first excerpt and 1.6–1.7 s subsequently; RoFormer took
10.4–11.3 s per excerpt. These are local pilot timings including per-file processing,
excluding model download/loading, not a general speed or quality ranking.
The reusable `separate-excerpt.py` tool was also run successfully on the Ehla excerpt,
including validation that every stem remains stereo and exactly 12 seconds long.

## Cost and checks

The rhythm state is **2,112 bytes** of static RAM. Each 16 ms call does at most
four lag comparisons / 1,280 sample pairs / eight square roots, amortised across
ten frames. There is no additional FFT, heap allocation, PCM history or on-device
ML dependency. Existing renderer sampling limits are unchanged.

Host checks pass with undefined-behaviour sanitization: EDM at 100/125/150/180 BPM,
slow upper-band rhythm at 75 BPM, irregular synthetic speech, quiet and loud noise,
60 Hz hum, own playback, listening interruption, isolated post-song noise,
bounded breakdown grace and both battery listening durations. Dance checks cover
continuous beatless sway, tempo transitions, all animation exits, lighting schedules,
spectrum cadence and existing compositor behavior. Rush, interaction, power and
trace tests pass; notebook/replay/comparison tests pass.

ESP-IDF 5.5.5 builds successfully. The image is about **817 KiB**, leaving 80% of
the 4 MiB app partition free. Host timing does **not** validate the ESP32's 1.5 ms
per-frame budget. Device worst-case analysis time and combined rendering FPS still
need measurement when this candidate is flashed; no firmware was flashed in this pass.

## Next useful data and work

1. Collect conversation (several voices/distances), TV/podcasts, household noise,
   and speech over music with the same device. Reserve entire tracks/sessions for
   validation rather than mixing nearby frames between tuning and validation.
2. Label expected response intensity separately: no response, gentle sway, groove,
   energetic dance. Keep boundary and beat-timing annotations distinct. Current
   binary labels cannot score whether a response was too energetic.
3. Add music/speech/singing scores from an offline audio-event classifier such as
   [YAMNet](https://github.com/tensorflow/models/tree/master/research/audioset/yamnet),
   checked against human labels. A vocal stem alone cannot distinguish singing
   from conversation. Explore a compact firmware music-continuity feature/classifier
   once the negative examples exist; do not just extend the hold indefinitely.
4. Use [Beat This!](https://github.com/CPJKU/beat_this) or aligned clean references
   to propose beat times, then verify them on microphone audio. This is separate
   from music admission and from the stem-separation experiment.
5. Characterise the observed upper-frequency roll-off with known tones before
   increasing sample rate or adding a second on-device FFT for stereo analysis.
