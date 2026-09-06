# One creature: audition round 1

Status: awaiting a listening choice. The owner previously chose
`sample_Junior_B_pitched_more.wav`; **A below uses that treatment**. The letters
in this round are new. No new treatment has been put in the firmware.

Each medley alternates a word phrase with an interjection, using the same Junior
voice and processing for both. Judge whether each pair belongs to one creature,
then articulation, attitude, and whether the interjections could replace the
procedural moods. These are synthesized interjections, not recorded laughter or
yawns; their spellings are prompts, not guarantees of a convincing performance.

| Candidate | Treatment | Length | WAV size |
| --- | --- | ---: | ---: |
| [A — Familiar](creature_A_familiar.wav) | Previous chosen B: resample ×1.55, 320 Hz high-pass, no modulation | 8.35 s | 261 KiB |
| [B — Round](creature_B_round.wav) | Pitch ×2.6 with formants preserved; tempo ×1.35; 400 Hz high-pass, 7 kHz low-pass | 9.10 s | 284 KiB |
| [C — Alloy](creature_C_alloy.wav) | B plus 90 Hz modulation at 22% depth and a short moving comb/flanger | 9.11 s | 285 KiB |

B/C raise pitch farther while allowing more time for articulation than A. This
is an exploratory treatment comparison: pitch, formants, and tempo differ
between A and B. B versus C isolates the metallic effect. There is no synthesized
speech carrier or extracted/resynthesized pitch track in this round.

The source is identical across candidates: Junior, 165 words/minute,
`[[pmod 60]]`, with the punctuation below. Formant preservation uses the host's
[FFmpeg rubberband filter](https://ffmpeg.org/ffmpeg-filters.html#rubberband).
The on-device path only needs to decode the finished samples.

## Clip order and start times

| # | Spoken prompt | Role | A start | B start | C start |
| ---: | --- | --- | ---: | ---: | ---: |
| 1 | hello | greeting | 0.00 | 0.00 | 0.00 |
| 2 | ooh! | delight | 0.70 | 0.77 | 0.77 |
| 3 | uh oh | worried phrase | 1.31 | 1.42 | 1.42 |
| 4 | aah! | surprise | 2.01 | 2.17 | 2.18 |
| 5 | seriously? | disbelief | 2.64 | 2.85 | 2.85 |
| 6 | hmm? | curiosity | 3.53 | 3.80 | 3.81 |
| 7 | whatever | dismissal | 4.19 | 4.51 | 4.52 |
| 8 | ugh | disgust | 4.99 | 5.39 | 5.39 |
| 9 | fuck you | insult | 5.61 | 6.06 | 6.07 |
| 10 | ha ha ha | laugh attempt | 6.41 | 6.95 | 6.96 |
| 11 | nice try | teasing | 7.17 | 7.78 | 7.79 |
| 12 | wheee! | celebration | 8.05 | 8.76 | 8.77 |

There is a 350 ms gap between clips, with no final gap. Files are 16 kHz,
mono, signed 16-bit PCM WAV. Silence is trimmed relative to each clip's peak
at −50 dB, with 8 ms before and 20 ms after; 2 ms edge ramps suppress clicks.
Corresponding utterances are RMS matched across A/B/C. For this render every
clip reached −18.42 dBFS RMS, and none clipped. RMS matching is not perceptual
loudness matching, especially through the actual speaker.

Numerical spectrum check: a 1024-sample Hann-windowed FFT with 512-sample hops
over each medley put approximately **33.52% / 0.03% / 0.43%** of energy below
400 Hz for A/B/C respectively. A is retained as the previously preferred
reference despite that low-frequency energy. This measurement does not prove
intelligibility or predict the speaker's response. Peak levels are −4.37 /
−5.44 / −3.95 dBFS. Listening quality has not been verified by the producing
agent; the owner chooses the treatment.

## Listen on Windows

From the repository root, after pulling the branch:

```powershell
git pull
Start-Process .\docs\voice\creature_A_familiar.wav
# Play the others separately:
Start-Process .\docs\voice\creature_B_round.wav
Start-Process .\docs\voice\creature_C_alloy.wav
```

Choose A, B, or C (or reject this round). It also helps to say whether the
interjections belong with the words and which words became hard to understand.

## Regenerate on macOS

```sh
tools/host/creature_round.sh
```

Requires the installed Junior voice, `say`, `afconvert`, gcc, FFmpeg built with
rubberband, and Python's standard library; no NumPy. `build.sh robot` builds the
existing plain-C baseline processor. The script regenerates all three WAVs and
[the manifest](creature_round1.json), including exact sample counts, timings,
levels, hashes, and OS/FFmpeg versions. Intermediate and individual finished
clips remain in ignored `tools/host/out/creature_round1/`. TTS and DSP versions
can change renders across machines; the committed WAVs define this audition.

## Playback/encoding handoff

The branch already contains playback plus 54 Junior B word clips, added after
the original handoff. The existing `main/clips_gen.h` contract is:

```c
typedef struct {
    const char *name;
    uint32_t samples;
    const uint8_t *data;  // samples / 2 bytes
} clip_t;
```

Samples are 16 kHz mono. The current codec is **raw IMA ADPCM**, not an IMA WAV
block stream: initialize predictor and step index to zero at each clip's start;
decode the low nibble before the high nibble; retain state between audio
frames. Counts must be even. Storage is exactly `samples / 2` bytes (8000 bytes
per second), without per-block headers. Only the audition medleys contain the
350 ms separators. The current player decodes 160-sample/10 ms blocks.

After a treatment is chosen, render the full existing word list plus approved
interjections, keep the existing 54 enum positions stable, append interjection
IDs, and encode with `main/adpcm.c`. Use descriptive, unique names for new
moods. `tools/host/clips.c` currently writes the live generated tables, so do
not run it on this 12-clip audition directory. The final bank needs a decoded
round-trip audition before adoption. These host effects add no on-board DSP;
the stated CPU budget still needs measurement on the board. Procedural babble
and touch purr remain a separate consistency decision after the listening pick.
