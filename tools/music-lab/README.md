# Music Lab

Local coordination page for labeled, full-track ESP32 microphone feature capture.
No server-side processing, external libraries, accounts, uploads or waveform storage.
The firmware does not change detection thresholds from these recordings automatically.

## Start

On the Mac, from the repository root:

```sh
tools/music-lab/serve.sh
```

On Windows with Python installed:

```powershell
py -m http.server 8765 --bind 127.0.0.1 --directory tools/music-lab
Start-Process http://localhost:8765
```

Open http://localhost:8765 in Chrome or Edge. This uses the browser's
[Web Serial API](https://developer.chrome.com/docs/capabilities/serial), which
requires a secure context and a user-initiated port selection; localhost qualifies.
Close `idf.py monitor`, the serial logger or any other program holding the port.
Click **Connect ESP32** and select its USB JTAG/serial port. The page starts the
feature stream; wake the companion with the power button if it is asleep.
The device's **Record music USB / Stop music capture** menu remains available too.

Name each run, choose music/speech/quiet/noise, and note style, playback level,
speaker/distance and any expected BPM. Start the run before playback, then mark
**Track starts**. Mark missed kicks, snares instead of kicks, false dancing,
breakdowns, tempo/loop changes and good tracking as they happen. End each run,
then start another. Download the session before closing the page. **Disconnect**
sends the device a stop command. Unplugging USB also ends capture. The page sends a heartbeat every two seconds;
90 seconds without control traffic ends abandoned capture and restores ordinary
sleep timing. A device-menu capture needs the page connected within that window.

The page can reopen `.mcal` sessions; expand a run to review markers or correct
its labels. Corrections update metadata without changing captured frames. New session
requires downloading any unsaved recording first. The simulated signal is clearly
labeled as a demo in both the page and file and cannot be mixed into a real run.

## Useful first collection

Start with the two troublesome tracks in full, at the usual volume. Add a few
contrasting styles, including sparse/breakdown-heavy music and dense rolling bass.
Repeat one or two at quieter and louder levels, keeping device placement stable.
Add speech/TV and a quiet-room run as negative examples. Keep some complete tracks
aside when tuning; otherwise we can improve familiar tracks while hurting new ones.
A useful first set is roughly 30–60 minutes, not hours of the same groove.

These labels are reference categories and human observations, not beat-accurate
ground truth. Marker timestamps use the latest device frame and have roughly
100 ms of transport delay, plus human reaction time. The page does not control
Spotify playback. It cannot tell from the title where a song started; mark it.

## What is retained

Every 16 ms analysis frame becomes an explicitly encoded 24-byte record:

| Bytes | Meaning |
| --- | --- |
| 0–3 | Device milliseconds, unsigned little endian |
| 4–5 | Full-band RMS, LSB |
| 6–11 | Kick RMS, pre-update kick mean, previous kick RMS; each uint16 in quarter-LSB units |
| 12–13 | Detector BPM × 10 |
| 14 | Presence × 255 |
| 15 | Bass ratio × 127.5 (range 0–2) |
| 16 | Tempo confidence × 255 |
| 17 | Speech modulation depth × 64 |
| 18 | Flags: candidate, accepted beat, speech, own voice, clipping, rush, dancing, listening |
| 19–21 | Normalized bass/mid/high × 255 |
| 22–23 | Device recording-session ID |

The host file is `MCALv001` (8 bytes), JSON-byte-count (uint32 LE), UTF-8 JSON,
then the records. JSON contains run frame ranges, labels, device configuration,
markers, summaries, one-second performance samples and transport warnings. Frame timestamps expose timing gaps;
queue overflow is reported explicitly. The page caps one session at 500,000
frames (about 133 minutes / 12 MB of feature data).

Storage is **90,000 bytes per minute**, about 450 KB for a five-minute track and
5.4 MB per hour, plus labels. Device flash receives **zero recording writes**.
The analyser makes a nonblocking copy to a 128-record queue (3,072 payload bytes).
A low-priority task with a 3 KB stack batches ASCII-hex packets directly to the
secondary USB console; it avoids sending the bulk stream through the slower UART
or holding the global log lock. There is no second FFT or audio capture pass.
The queue/task are allocated once, on first use, and reused. The USB driver also
reserves 2 KB TX and 256 bytes RX plus its bookkeeping at startup. When idle, the writer
blocks on its queue. USB control reads are bounded and nonblocking, every 100 ms.

Capture keeps microphones continuously active and the device awake, suppresses
spontaneous speech/effects, and preserves ordinary music admission thresholds.
This deliberately differs from normal intermittent listening. Entering the mic
axis wizard ends music capture because that wizard uses a different gain.
Dancing/listening flags reflect the render task's latest state (up to one frame
behind), including manual dance. Own playback and clipping are separately flagged.

This is enough to tune onset gates, confidence, presence and session timing. It
cannot replay different frequency filters or recover the original sound: those
experiments still need source audio or a fresh run. No automatic global sensitivity
adjustment is made from music-only evidence.

## Offline inspection and reproduction

Plain-C export, without NumPy or another runtime dependency:

```sh
tools/host/build.sh music_report
tools/host/bin/music_report session.mcal frames.csv labels.json
```

Compare detector candidates and accepted beats around marked sections; assess
false positives on speech/noise and keep whole-track holdouts. Any resulting
threshold change should be checked against the existing audio regressions and
replayed on held-out recordings before applying it to firmware.

Tests:

```sh
tools/host/build.sh trace_test -DAUDIO_ANALYSIS_HOST -fsanitize=undefined -fno-sanitize-recover=all
tools/host/bin/trace_test
node tools/music-lab/trace.test.mjs
```

The Node check only tests the browser's plain-JavaScript codec; Node is not needed
for using the page or the C exporter.
