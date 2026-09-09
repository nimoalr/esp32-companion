# First recording: full stem analysis

Analysis date: 2026-09-09. This is the same tuning corpus as
[pass 1](CALIBRATION_PASS_1.md), not an independent accuracy evaluation.
The evaluated firmware replay is `7f42bcf`; **no detector or firmware changes were
made in this pass**. All microphone PCM, model outputs and comments remain local.

## Scope and method

All nine runs / 1,619.36 seconds now have four complete stems from Audio Separator
0.47.0 with `htdemucs.yaml`. The full-track worker uses 60-second core passages,
two seconds of context on either side, six-second Demucs segments, shifts 0 and
overlap 0.1. Results are cropped onto the original 16 kHz stereo timeline. This
extends the earlier three 12-second pilots to the full corpus, including quiet.

Stem RMS is stored every 100 ms, quantized to 0.1 dBFS. Range summaries average
**linear power**, weighted at boundaries, then convert to dB. Relative power means
stem mean-square / microphone mean-square, not an instrument probability; stems
need not sum to the mixture. Firmware decisions retain their 16 ms cadence.
Positive-duration totals use the union of explicit positive labels, excluding
conflicting negatives and unlabelled/unsure-only time. Individual table rows can
overlap. The source has no gaps. Original records, metadata and annotations were
verified unchanged in the new notebook copy.

## What this clarifies

The previous pass still misses 272.085 of 625.859 explicitly labelled music seconds.
Most of those misses coincide with weak **estimated** drums:

| Descriptive drum-level cutoff, relative to the mixture | Missed seconds below cutoff | Share of all missed music |
|---|---:|---:|
| −15 dB | 219.806 | 80.8% |
| −20 dB | 206.094 | 75.7% |
| −25 dB | 196.534 | 72.2% |

These cutoffs are sensitivity probes, not new absence labels. The conclusion
survives a ten-decibel change in cutoff: a more sensitive kick trigger alone is
unlikely to address most remaining failures. Separation mistakes remain possible;
for example, Schrottenhagen's first annotated “intro with beat” has estimated drums
at −36.5 dB. The human label must not be overwritten by that estimate.

| Passage, seconds on the microphone timeline | Drums / mixture | Strongest other stem | Current music-state coverage |
|---|---:|---|---:|
| Ehla clear beat, 15.397–147.869 | −4.9 dB | Accompaniment, −4.5 dB | 92.0% |
| Schrottenhagen run 2 opening, 0–46.580 | −19.7 dB | Accompaniment, −1.7 dB | 0% |
| Silver Lines chords, 91.252–114.096 | −54.0 dB | Accompaniment, ~0 dB | 100% |
| Silver Lines continuation, 114.096–150.121 | −24.6 dB | Vocals, −3.3 dB | 0% |
| Angèle intro, 4.661–52.251 | −15.3 dB | Vocals, −1.8 dB | 0% |
| Angèle vocal section, first 20 s, 146.947–166.947 | −50.2 dB | Vocals, −1.7 dB | 1.3% |

### Admission and continuity are separate problems

**Silver Lines exits at 114.096 s** and does not return until 188.160 s. The labelled
gentle section runs 91.252–150.121 s; it is audible, not a silent gap. Its continuation
after the exit is above the rhythm helper's loudness threshold on 99.9% of frames,
but never accumulates sufficient rhythm evidence. The code's 45-second limit
without confirmed rhythm is consistent with this exit. Extending that timer would
mask one symptom; it would not recognize a beatless opening.

**Angèle exits at 147.216 s**, almost exactly where the requested gentle vocal
passage starts. During its first 20 seconds, estimated vocals dominate, loudness
is above the helper threshold throughout, and the new rhythm route never qualifies.
Its entire 4.661–52.251 s intro also stays above that loudness threshold but has
zero admitted music. This calls for evidence of music beyond repeating percussion,
both to admit gentle openings and to sustain an already recognized song.

The existing drive signal already becomes low in several appropriate sections
(Ehla outro ~0.08, Schrottenhagen's first long buildup ~0.08). However, low drive
cannot render a sway once music admission has been lost. Drive values in replay
are control signals, not human intensity ratings, and they can exist while the
device is outside music mode.

### Beat tracking still needs a distinct evaluation

A host Welch spectrum of estimated drums (2,048 samples, 50% overlap, average
stereo power) places only 0.35%, 0.61% and 0.56% below 80 Hz in the Ehla clear-beat,
Burnin early-beat and Silver Lines opening ranges, respectively. Their strongest
energy bands are higher. This supports testing broader or multiple percussion
bands for timing. The current 80 Hz low-pass is not a brick-wall filter, and total
spectral energy does not identify the perceptual main beat. These measurements
**do not prove improved kick-versus-snare alignment**. Drum transients can propose
events for review; verified beat annotations are still needed before scoring them.

### A vocal stem is not a speech/music classifier

The quiet-room recording produces estimated vocals at −6.9 dB relative to its
mixture despite having no labelled music. Absolute mixture level is only −64.9
dBFS, and the firmware correctly stays out of music. A relative “vocals present”
rule would therefore be unsuitable. The device speech flag also fires during
25.1% of Dance With Me and 64.1% of Talk To You. It measures speech-like modulation,
not reliable conversational speech. This corpus still lacks conversation, TV and
speech-over-music negatives, so it cannot validate a broader music classifier.

## Next detector experiments, in priority order

1. Separate **music confidence**, **pulse confidence** and **movement intensity**.
   High music confidence with weak percussion should select gentle motion. Strong
   pulse confidence should add timed movement; intensity should control its size.
2. Prototype music-continuity evidence on the laptop from the original microphone
   mixture. Use stems to locate accompaniment/vocal passages for inspection, not
   as inputs that the ESP32 will have. Candidate low-rate spectrum/modulation
   features must distinguish sustained music from voiced speech and hum; tonality
   alone is insufficient. Evaluate against the new negative recordings before
   relaxing admission or renewing continuity with them.
3. Evaluate entry and retention separately. A longer breakdown allowance cannot
   solve the zero-coverage Schrottenhagen and Angèle openings. Keep the existing
   quick genuine-silence exit while testing continuity evidence.
4. Propose drum events from multiple frequency bands for human beat verification.
   Measure misses, false events, timing offsets and half/double-tempo errors
   separately from music-state coverage. Do not equate every snare with a false beat.
5. Add explicit human intensity labels (gentle, groove, energetic) before tuning
   movement size. Existing positive labels sometimes explicitly request no vigorous
   dance, so binary coverage alone cannot evaluate the desired personality.

No thresholds were loosened using model stems as pseudo-ground-truth in this pass.

## Storage: one self-contained notebook

Measured on all 36 stems, without changing sample rate or channels:

| Payload | Size, decimal MB |
|---|---:|
| Original notebook | 110.7 |
| Added level curves/provenance | 0.410 |
| Four stems as PCM WAV | 414.6 |
| Same stems as lossless FLAC | 98.8 |
| Notebook with curves + FLAC payload, before container overhead | 209.9 |

Every FLAC was decoded and compared byte-for-byte with its PCM stem. Compression
reduces stem storage by 76.2%; a portable copy would be about 1.9 times the original
notebook here rather than 4.7 times with raw stems. Results depend on the recording.

The implemented `MCALv004` format includes all generated FLAC stems as binary
attachments with source/model/sample-count identity and per-attachment CRC32.
**Save session always includes generated stems.** Playback after reopening uses
embedded audio; it does not rely on the server. The v1–v3 readers remain supported,
and the C exporter reads the original records without mistaking attachments for
detector frames. The portable file limit is 1 GB, with bounded 16 MB JSON metadata.

There is no persistent audio cache. New analyses use temporary handoff files,
deleted once all stems have been received by the browser. Abandoned handoffs expire
after ten minutes idle, and normal shutdown removes the temporary directory. The
old audio cache and redundant generated audio copies were removed after verifying
the portable file. Installed software/model weights remain available. Neither the
original recording nor human labels were modified.

## Local artifacts and reproduction

On the owner's Mac, Downloads contains:

- `companion-first-recording-with-stems.mcal`: 209.9 MB, with all nine recordings,
  original annotations and 36 embedded FLAC stems; open it in the updated Music Lab.
- `companion-first-recording-stem-analysis/`: numbered source-verified references,
  `report.json`, `drum-spectra.json`, this report and a PCM-hash/size verification
  manifest. There are no loose stem audio files in this analysis directory.

```sh
node tools/music-lab/export.mjs original.mcal export-directory
# Use the same aligned 7f42bcf audio_replay CSVs as pass 1, named NNN-candidate.csv.
python3 tools/music-lab/stem-corpus.py export-directory reference-directory
python3 tools/music-lab/stem-report.py export-directory reference-directory > report.json
node tools/music-lab/attach-stem-references.mjs original.mcal reference-directory new.mcal reference-directory
# Optional spectrum inspection using the ML environment's NumPy/SciPy:
~/.cache/companion-music-lab/venv/bin/python tools/music-lab/stem-spectrum.py \
  drums.flac --start 15.397 --end 147.869
```

Use temporary working directories for exported audio and remove them after verifying
the final notebook. The batch shares the local server's single-job queue and
reuses verified existing local exports when rerun.
A macOS C++ destructor crash after successful separation was encountered on the
first full Day Din run. The one-job worker now exits directly only **after** all
audio files are closed and final JSON is atomically saved/flushed; inference exceptions
still fail normally. That run and the remaining uncached full tracks then
completed successfully. Fresh FLAC output, transfer into the browser, immediate
server-file deletion, export/reimport and offline stem playback were also tested.
Report tests cover linear-power averaging, partial bins,
overlap/conflict handling and exact clock association. Original and enriched
notebooks have all 101,210 records bit-identical and all human metadata unchanged.
