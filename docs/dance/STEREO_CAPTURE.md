# Stereo capture and timeline review

The USB recorder now optionally retains **both ES7210 channels**, losslessly at
16 kHz / PCM16, from the same interleaved I2S frame consumed by the analyser.
The page defaults to stereo; compact feature-only mode remains available before
connecting. A static USB MUSIC / RECORD MODE message is painted once at 20%
brightness. A tap exits after a 500 ms entry guard; PWR hold and MC_STOP remain
available. No ongoing raster/display updates are added to recording mode.

The v3 packet binds one 256-sample/channel audio frame to its existing 64-byte
feature record with a single sequence number and CRC32. Storage is 4.1 MB/minute;
queue data occupies 136.5 KiB in PSRAM, with fixed internal buffers and a 4 KiB
writer stack. Capture still runs at 62.5 analysis frames/s; its sample rate and
music thresholds are unchanged. Higher sample rates are an experiment, not an
assumed improvement in low-frequency beat detection.

## Hardware verification

On the flashed ESP32-S3, a 125-second no-heartbeat stereo transport check received
7,805 consecutive frames: 62.5105 frames/s, zero sequence gaps, zero checksum
errors, and zero reported device queue drops. Maximum analysis CPU was 552 µs;
maximum measured packing/enqueue CPU was 86 µs. Even adding these separate maxima
is below 0.64 ms per 16 ms audio frame. Writer stack headroom was 2,352 bytes.
There were 1,917,388 unequal L/R sample pairs, verifying this was not mono copied
into two channels. MC_STOP resumed the normal renderer; no watchdog reset/panic
occurred during the capture. This is a short soak, not a multi-hour guarantee.

A separate real browser recording retained 1,297 stereo frames (20.752 sample-clock
seconds) with zero sequence gaps and zero queue drops. Reported maximum analysis
CPU was 384 µs, packing/enqueue 78 µs. Left/right peaks and RMS differed, with no
clipping. Its private PCM/notebook remain local and are not committed.

## Review experience and offline checks

The browser editor has a time ruler, named stereo/detector/annotation lanes,
click-to-seek, drag selections, resize handles, zoom/pan/fit/zoom-to-selection,
selection playback and looping, playback speed and separate-channel audition.
Human labels use exact ranges with free-form comments and dance/no-dance/unsure
expectations. Annotation cards support edit/delete/undo; capture diagnostics are
collapsed, and Save session is available in the editor toolbar.

Browser checks exercised creating a 3.250–7.500-second annotation, zooming to it,
looping playback, deletion/undo, export and native-file-picker reimport with the
annotation preserved. Direct mouse dragging selected 8.272–18.835 seconds; dragging
its right edge extended it to 21.806 seconds without changing its start. Stereo,
left/right preview and original WAV export were checked on a clearly marked
synthetic fixture; a screenshot review checked the final editor layout.

Host checks cover C-to-JS stereo framing/CRC, bit-exact channel order and PCM WAV,
missing frames retained as silence, legacy/mixed imports, channel statistics,
range bounds and conflicting annotation scoring. The C analyser replay accepts
real stereo WAV without summing/duplicating channels; pass gain 0 to preserve
levels. `export.mjs` and `compare-replay.mjs` allow original decisions and new C
reanalysis to be scored against the same human-labelled passages. Reanalysis
starts from fresh detector state; original recorded flags are retained separately.
Existing audio regression tests and the ESP-IDF build passed.

See [Music Lab](../../tools/music-lab/README.md) for protocol details, workflow,
limits, and proposed local YAMNet / stem-separation comparisons. No ML model has
been installed, no automatic ground truth is asserted, and no detector tuning is
claimed by this recording/editor change.
