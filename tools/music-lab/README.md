# Music Lab — dedicated USB recording

Run `tools/music-lab/serve.sh`, then open http://127.0.0.1:8765/ in Chrome/Edge.
The page uses Web Serial; no accounts, cloud, raw audio recording, or device flash writes.

1. Connect ESP32. `MC_START` enables exclusive recording: the screen goes dark,
   animations/personality/rendering and display transfers stop, microphone analysis stays on.
2. Label each reference, Start run before playback, mark issues, End run afterward.
   Ending a run keeps the device in recording mode for the next track.
3. Download the notebook before closing/reloading the page.
4. Disconnect explicitly sends `MC_STOP`. Alternatively hold PWR for two seconds.
   Unexpected USB loss or a backgrounded browser **does not end recording mode**.
   There is no heartbeat lease. Reconnect to resume; a finite queue cannot preserve
   all samples during an arbitrarily long disconnect, and gaps are reported.

The normal microphone gain and music-admission thresholds are unchanged. A cheap
shadow behavior update labels whether the detector **would dance/listen**; these are
not rendered emotions. Own voice and impact effects are inhibited. On exit the
normal renderer/personality resume and the normal inactivity countdown restarts.

## Cadence and transport

Audio remains 16 kHz stereo, 256 samples/channel per analysis frame: **62.5 feature
frames/s**. Display FPS is zero during capture and is not a capture-rate metric.
Shortening the audio window would reduce the current 62.5 Hz FFT-bin resolution,
so this change improves reliability without changing detector timing or FFT size.

A low-priority writer on core 1 sends at most eight records per CRC32-protected
`MC2:` hexadecimal line. It preserves partial writes and retries nonblocking USB
writes. Audio enqueue never waits. Queue: 128 × 64 bytes (~2 seconds), writer stack
4 KiB, USB TX ring 4 KiB. A stalled reader eventually causes counted queue drops;
sequence numbers additionally expose transport gaps. Stop can discard pending
transport data; the page ends an active run and retains already received records.

`MC_START` is idempotent; reconnecting does not reset a live session or its sequence.
`MC_PING` requests status, not a lease renewal. `MC_STATE` reports produced frames,
queue drops and renderer state. A host that stops reading cannot block the audio
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
Readers still accept `MCALv001` / 24-byte records; mixed exports promote legacy
records with missing-spectrum markers. Storage is ~14.4 MB/hour plus metadata.
The page caps sessions at 500,000 frames and imports at 64 MB. No automatic
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
