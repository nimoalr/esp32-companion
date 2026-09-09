# YouTube speech references: first replay inspection

Analysis date: 2026-09-09. Evaluated source revision: `bd0bca3`.
No firmware settings or detector implementation were changed or flashed.

## Evidence

The portable notebook `companion-music-2026-09-09T05-18-54 (1).mcal`
contains two runs tagged speech, 248.432 and 356.784 seconds, with eight
embedded stems. Both were recorded at usual volume from laptop speakers,
with the companion behind the screen on a sofa. All 37,826 stereo frames
are present; neither channel clips and there are no sequence gaps.

The owner subsequently confirmed that video 2 has **no background music**,
while video 1 has background music toward the end. The owner also judges the
separation more accurate on video 1. These human observations take precedence
over any stem estimate suggesting accompaniment in video 2.

The second run's 326.066–329.981 s annotation requests that an accidental
music passage be ignored. Preserve that explicit exclusion even though the
video itself has no background music. The start of background music in video 1
has not been precisely labelled; do not invent a boundary from the stems.
Consequently video 2 is a speech-only negative outside the exclusion, whereas
video 1 contains a mixed speech/music passage and is not a whole-run negative.
No formal range-label accuracy score is reported.

The exact C analyser and behavior engine were replayed on original 16 kHz
stereo PCM, target RMS 0 (no level normalization), starting fresh for each
run. Model curves were verified against the exported WAV SHA256 and exact
sample count with `stem-report.py`. Levels below average linear power over
time; relative stem power is not a classification probability.

| Measurement | Video 1 | Video 2 |
|---|---:|---:|
| Duration | 248.432 s | 356.784 s |
| Current replay time in music mode | 0 s | 45.360 s |
| Current replay music-mode coverage | 0% | 12.71% |
| Speech flag coverage | 98.79% | 99.52% |
| Maximum accumulated second-route rhythm evidence | 0 | 0 |
| Estimated vocals relative to mixture | −0.17 dB | −0.48 dB |
| Estimated drums relative to mixture | −30.77 dB | −30.73 dB |
| Estimated accompaniment relative to mixture | −18.36 dB | −12.22 dB |

The recorded device music flags cover 10.38% and 29.29%, respectively.
These are different from fresh-state replays: the capture identifies firmware
`71ed083-dirty`, and carries live state from before recording. Those numbers
must not be presented as a controlled before/after improvement.

## Confirmed speech-trigger failure

Video 2 enters music at **40.400 s**, leaving at **85.760 s**. This is separate
from the accidental music passage marked for exclusion later in the recording.
At entry, the original kick gate sees 120.97 BPM, tempo confidence 0.75,
sub-bass ratio 0.229, loudness 747.65, and the speech flag is true. The newer
rhythm route has correlation 0.21 and accumulated evidence 0: it did not
cause admission.

The old gate is satisfied over 40.400–40.736 s and 40.752–40.768 s. The first
interval contains one detected onset and the second none. A brief apparently
regular sequence is enough for immediate admission; the 45-second breakdown
allowance then keeps the music state alive long after that support disappears.
Over a four-second window around entry, estimated vocals are −0.03 dB relative
to the mixture, while estimated drums and accompaniment are −34.87 and
−33.64 dB. The owner's confirmation that this video has no background music
establishes this as a false music admission. The stems are supporting diagnostics,
not the ground truth; their accompaniment estimate elsewhere in this video
illustrates separation leakage or error.

This suggests testing **persistent initial confirmation**, or a short provisional
music state, before granting a full breakdown allowance. It does not justify
shortening the allowance for an already established song.

A screen of the first corpus's existing replay CSVs exposes a tradeoff: Angèle's
three old-gate episodes are all at most 0.416 s and none contains two qualified
onsets. Requiring a longer continuous gate or multiple qualified onsets globally
could worsen this already difficult music example. That CSV screening is a
hypothesis check, not a replay of a modified state machine. Do not deploy a
blanket duration threshold on this evidence alone.

## What the stems add to intensity work

The speech-only passages help test whether bass in a speaking voice is being mistaken
for musical percussion. Mean `dance_drive` is 0.167 and 0.229 over these runs;
it averages 0.245 during the second run's music interval. Thus drive can be
nonzero on speech: it is an animation control signal, not proof of music.

The owner's target is **content-aware movement, not speech priority**:

| Audible content | Intended response |
|---|---|
| Speech-led video / conversation, no music | Normal personality/listening motion; no music-driven dance |
| Speech-led video with background music | Gentle musical movement is welcome; avoid escalating to a full dance show just because the background has a beat |
| Music-led passage, including singing | Dance intensity follows the passage: gentle for sparse parts, stronger for energetic parts |
| Uncertain mixture or transition | Blend conservatively and smoothly; avoid frame-to-frame state changes |

The first video's zero music-mode coverage is therefore **not an unqualified
success**: its background-music ending is a candidate missed gentle-response
case. Its precise onset and desired movement strength still need range labels.
Video 2's false entry is an established speech-rejection case.

Distinguish speech-led from music-led acoustic context over time. Literal source
identity (TV versus a music player) cannot always be inferred from the same
sound; a song played inside a show can be acoustically indistinguishable from
that song played separately. Spoken narration, musical accompaniment, singing,
continuity and pulse are relevant evidence; device/app identity is not the target.
A vocal stem includes both singing and speaking and cannot supply that distinction.

Keep acoustic content labels separate from desired movement labels in subsequent
calibration: speech / speech with background music / music / uncertain describes
what is audible, while none / gentle / groove / energetic describes the wanted
musical movement. Existing binary labels remain valid, but do not capture this
entire target. Do not rewrite them using separator output.

The first music corpus remains essential: its beatless vocal/chord passages
should retain gentle motion. Keep three distinct decisions:

1. **Music evidence:** whether a musical passage is established; speech dominance
   and prior song context matter, and a vocal stem alone cannot resolve it.
2. **Pulse evidence:** whether timed beat motion is justified. Weak pulse should
   not force an established gentle song out of music mode.
3. **Intensity:** continuous movement amplitude and lighting strength, with
   smoothing between gentle sway, groove and energetic movement.

Use stems on the laptop to locate relevant transitions and inspect separation
errors. Any feature intended for firmware must be derived from the original
microphone mixture; the ESP32 will not have those stems. Test candidate features
at low rate using existing bands/history before adding FFT work or a model.

Next evaluation should pair these speech references with the original annotated
music passages, scoring false entry duration, entry delay, breakdown retention
and intensity separately. Explicit gentle/groove/energetic range labels would
make intensity tuning measurable. Keep future speakers/videos/tracks as a
held-out set; these two recordings share one playback setup and are insufficient
for a broad accuracy claim.

## Reproduction and storage

Use an OS temporary export directory:

```sh
node tools/music-lab/export.mjs 'session-with-stems.mcal' "$work"
tools/host/build.sh audio_replay
tools/host/bin/audio_replay "$work/001.wav" 0 "$work/001-candidate.csv"
tools/host/bin/audio_replay "$work/002.wav" 0 "$work/002-candidate.csv"
# Save each numbered metadata file's stemReference object as NNN-reference.json.
python3 tools/music-lab/stem-report.py "$work" "$work"
```

No separation was rerun. The self-contained notebook is unchanged. Temporary
WAV/FLAC exports and replay CSVs were removed after this inspection; this report
retains the findings without creating another persistent audio store.
