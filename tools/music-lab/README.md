# Music Lab — dedicated USB recording

Run `tools/music-lab/serve.sh`, then open http://127.0.0.1:8765/ in Chrome/Edge.
The page uses Web Serial; no accounts, cloud, uploads, or device flash writes. Stereo microphone recording is enabled by default; untick it before connecting for compact features only.

1. Connect ESP32. `MC_START_PCM` enables exclusive stereo recording. A static USB MUSIC / RECORD MODE message is drawn once at 20% brightness, then animations/personality/rendering and display transfers stop. Microphone analysis stays on.
2. Label each reference, Start run before playback, mark issues, End run afterward.
   Ending a run keeps the device in recording mode for the next track.
3. Download the notebook before closing/reloading the page.
4. Disconnect explicitly sends `MC_STOP`. Alternatively tap the screen or hold PWR for two seconds. Entry touches are drained and a 500 ms guard prevents the menu tap immediately cancelling capture.
   Unexpected USB loss or a backgrounded browser **does not end recording mode**.
   There is no heartbeat lease. Reconnect to resume; a finite queue cannot preserve
   all samples during an arbitrarily long disconnect, and gaps are reported.

The normal microphone gain and music-admission thresholds are unchanged. A cheap
shadow behavior update labels whether the detector **would dance/listen**; these are
not rendered emotions. Own voice and impact effects are inhibited. On exit the
normal renderer/personality resume and the normal inactivity countdown restarts.

## Cadence and transport

Audio remains 16 kHz stereo, 256 samples/channel per analysis frame: **62.5 feature
frames/s**. Display update FPS is zero after the static entry message and is not a capture-rate metric.
Shortening the audio window would reduce the current 62.5 Hz FFT-bin resolution,
so this change improves reliability without changing detector timing or FFT size.

A low-priority writer on core 1 sends one stereo record per CRC32-protected `MC3:` hexadecimal line, or up to eight feature-only records per `MC2:` line. It preserves partial writes and retries nonblocking USB
writes. Audio enqueue never waits. Queue storage: 128 × 1,092 bytes in PSRAM (~136.5 KiB / 2 seconds), with its control structure in internal RAM. Writer stack 4 KiB, USB TX ring 4 KiB. Static producer/writer buffers avoid growing the audio stack. A stalled reader eventually causes counted queue drops;
sequence numbers additionally expose transport gaps. Stop can discard pending
transport data; the page ends an active run and retains already received records.

`MC_START_PCM` and `MC_START` are idempotent for the same stream format; changing formats starts a new device session. At the same format, reconnecting does not reset a live session or its sequence.
`MC_PING` requests status, not a lease renewal. `MC_STATE` reports produced frames,
queue drops, renderer state, maximum packing/enqueue CPU time and writer stack headroom. A host that stops reading cannot block the audio
or control loops. Existing UART/log output is not used for feature transport.

## Version 2: 64 bytes/frame, about 240 KB/minute

All integers little-endian. The first 24 bytes retain the v1 layout:

| Bytes | Contents |
|---|---|
| 0–3 | Device milliseconds |
| 4–5 | Raw stereo RMS, ADC LSB |
| 6–11 | Kick, running mean and previous kick RMS, quarter-LSB units |
| 12–13 | BPM × 10 |
| 14–17 | Presence, bass ratio, confidence, speech modulation depth |
| 18 | Candidate/beat/speech/own-output/clipping/rush/would-dance/would-listen flags |
| 19–21 | Existing gain-adjusted bass/mid/high display levels |
| 22–23 | Session ID |
| 24–27 | Sequence, incremented for every offered frame, including queue drops |
| 28–29 | Analysis CPU time, microseconds (excludes packet packing/enqueue) |
| 30–31 | Peak ADC magnitude; 65535 marks a converted legacy record |
| 32–63 | Sixteen raw FFT band powers, positive bfloat16 |

Raw spectrum is the sum of squared FFT magnitudes in each band **before automatic
band gain and presence weighting**, with the existing Hann window and mono mix.
Bfloat16 preserves the float exponent range with approximately 0.4% relative
rounding error, using bit operations on the device. It is not calibrated SPL.
Band edges in Hz: 62.5, 125, 187.5, 250, 312.5, 375, 437.5, 562.5, 750, 1000,
1312.5, 1750, 2375, 3187.5, 4250, 5687.5, 8000. DC and the Nyquist bin are omitted.

The page saves each track’s mean band power and integrated energy percentages.
The corpus view includes equal-track weighting and pooled energy weighting,
separately for music, speech, quiet and other noise. These answer different
questions; long/loud tracks dominate pooled energy. Compare matched microphone
gain/setup, and remember the bands have unequal widths. Legacy recordings retain
all old data but cannot supply a reliable raw frequency split retroactively.

`.mcal`: `MCALv002`, 32-bit JSON length, JSON metadata, then 64-byte records.
Readers still accept `MCALv001` / 24-byte records; mixed exports promote legacy records with missing-spectrum/audio markers. Storage is ~14.4 MB/hour plus metadata.
The page caps sessions at 500,000 frames or 128 MB of record data (whichever comes first), and imports at 256 MB. Stereo runs reach the byte cap after roughly 31 minutes; download and start a new notebook for another batch. No automatic
sensitivity retuning is performed; retain speech/noise negatives and held-out tracks.

## Offline tools and checks

```
tools/host/build.sh music_report
tools/host/bin/music_report session.mcal frames.csv labels.json
tools/host/build.sh trace_test -DAUDIO_ANALYSIS_HOST -fsanitize=undefined
tools/host/bin/trace_test
node tools/music-lab/trace.test.mjs
```

The plain-C exporter accepts both versions and includes raw powers, sequence,
analysis time and peak for v2. Browser codec tests cover legacy/mixed imports,
CRC corruption, saturation, gaps, exact frame rate and corpus aggregation.

## Version 3: lossless stereo microphone audio

Each 1,092-byte record contains the unchanged 64-byte v2 features, ASCII `PCM1`
at bytes 64–67, then 1,024 bytes of interleaved signed little-endian PCM16 at
bytes 68–1091: L0, R0, L1, R1, …, L255, R255. These are **both actual ES7210
microphone channels from the same I2S read used by the detector**, before its
mono mix, windowing or digital feature normalization. The configured analogue
microphone gain still applies. There is no lossy codec, resampling, host microphone,
or reconstruction from the feature values.

Transport is about 137 KB/s including hex framing; stored records are 68,250 B/s,
about 4.1 MB/minute (3.84 MB/minute PCM plus features/tag). File header `MCALv003`
uses the same JSON-length/notebook structure followed by 1,092-byte records.
Converted v1/v2 records have no `PCM1` tag and must not be treated as recorded
silence. CRC covers PCM and features together; their sequence/timestamp association
cannot drift through separate stream losses.

## Listen and label afterward

Expand an ended run and choose **Replay & label**. The two waveform rows are
left/right microphones. Under them are the recorded would-dance / would-listen
intervals and your human labels. Click to seek; drag to select a range and drag its handles to refine it. Zoom in/out, fit the track or zoom to the selection, and pan with the position slider. Play or loop the selected passage. Space toggles playback, I/O set range edges, and +/− zoom. Precise start/end seconds remain available in a disclosure. Add **Should dance**, **Should not
dance**, or **Comment only / unsure**, with arbitrary notes. Labels appear as timeline regions and editable cards, with deletion and a 30-step undo history; download the notebook again to retain edits. They never overwrite
recorded detector decisions. Playback is local; the page does not auto-play.

Choose stereo, left, or right audition. Boost quiet replay changes only the
preview (peak-based gain, capped at ×100). Original stereo WAV export preserves
ADC sample values, channel order and levels. Sequence gaps become marked silence
instead of compressing time. Across a device-session reset, unknown missing time
is explicitly marked at the boundary; it cannot be reconstructed. Human labels
use seconds from the first received frame on this sample-clock timeline, not
Spotify's playhead or host arrival time. No audio is available for old feature-only
runs. Existing live markers retain their device/host timestamps in the notebook.

The annotation score reports duration correctly/incorrectly dancing only within
explicit human-labelled ranges with microphone audio. Missing audio, unlabelled
ranges and conflicting overlapping labels are excluded. These are training/evaluation
aids, not claims about accuracy on untested music. Keep entire tracks, speakers,
rooms and sessions out of tuning when constructing a held-out evaluation set.

Dependency-free local export and exact firmware analyser replay:

```sh
node tools/music-lab/export.mjs session.mcal /tmp/music-export
tools/host/build.sh audio_replay
tools/host/bin/audio_replay /tmp/music-export/001.wav 0 /tmp/reanalysis.csv
node tools/music-lab/compare-replay.mjs session.mcal 1 /tmp/reanalysis.csv
node tools/music-lab/replay.test.mjs
```

Export produces numbered stereo WAVs and JSON containing run metadata, original
annotations, gaps, channel RMS/peaks/clipping/correlation and recorded-label scores.
It refuses to overwrite existing outputs. The C analyser now accepts stereo WAV
and keeps channels separate; pass `0` to preserve recorded levels. Reanalysis starts
with fresh detector state, unlike a track recorded in the middle of an existing
capture session; the notebook's recorded flags remain the original evidence.

## Laptop models: useful next comparisons

[YAMNet](https://github.com/tensorflow/models/tree/master/research/audioset/yamnet)
is a candidate independent audio-event baseline: 521 classes including speech and
music, taking 16 kHz mono input. Keep stereo originals and compare channel-wise
versus mono results. Its predictions should be suggestions, especially for singing,
speech over music, TV and rhythmic non-music. It requires a separate Python/ML
environment; this repository's dependency-free recording page does not install it.

[Demucs](https://github.com/facebookresearch/demucs) separates drums, bass, vocals
and other instruments. It could help inspect kick/snare confusion offline; vocals
are not a speech-versus-music label, and separation artifacts are not ground truth.
No stem/model inference is installed or run by this change. WAV export makes those
experiments possible without altering firmware or exposing recordings to a server.

Capture stays at the detector's native 16 kHz. More sample rate would add high
frequencies but not improve timing or low-frequency resolution by itself. A useful
first offline experiment is overlapping longer FFT windows and temporal models
on these lossless samples, with labelled hold-out evaluation before changing the
shared I2S clock or on-device detector.
