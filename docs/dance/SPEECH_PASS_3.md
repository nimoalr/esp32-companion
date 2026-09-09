# YouTube speech references: first replay inspection

Analysis date: 2026-09-09. Evaluated source revision: `bd0bca3`.
No firmware settings or detector implementation were changed or flashed.

## Evidence

The portable notebook `companion-music-2026-09-09T05-18-54 (1).mcal`
contains two runs tagged speech, 248.432 and 356.784 seconds, with eight
embedded stems. Both were recorded at usual volume from laptop speakers,
with the companion behind the screen on a sofa. All 37,826 stereo frames
are present; neither channel clips and there are no sequence gaps.

The second run's 326.066–329.981 s annotation requests that an accidental
music passage be ignored. It is not a negative training example. There are
no other precise range labels; speech run tags are useful context but do
not establish the absence of background music at every instant. No formal
range-label accuracy score is reported.

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

## Concrete suspected speech-trigger failure

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
−33.64 dB. This strongly supports a speech-trigger interpretation, but model
separation is not human listening verification.

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

These negatives help test whether bass in a speaking voice is being mistaken
for musical percussion. Mean `dance_drive` is 0.167 and 0.229 over these runs;
it averages 0.245 during the second run's music interval. Thus drive can be
nonzero on speech: it is an animation control signal, not proof of music.

The first music corpus remains essential. Its beatless vocal/chord passages
should retain gentle motion, whereas speech should follow the owner's chosen
speech/background-music policy. Keep three distinct decisions:

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
