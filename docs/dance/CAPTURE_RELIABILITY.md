# Dedicated capture and frequency coverage

The owner's “Close the Curtains” reference exposed a difference between display
FPS and useful captured coverage. The saved legacy run contains 11,965 records:
191.4 seconds of 16 ms frames over 220.8 seconds of host recording, across four
device sessions. Display telemetry ranged from 17 to 60 FPS. Its old gap metric
reported zero because it did not include missing time between sessions or after
samples stopped. No transport events were recorded, so that file alone cannot
identify the precise cause of each interruption.

The code had a 90-second heartbeat lease and stopped capture when a USB-power
reading disappeared. It also discarded the return value from a nonblocking USB
write, losing any unwritten tail. The renderer, animations and PSRAM/display
traffic continued during capture. This change removes those failure paths:

- Explicit recording mode parks rendering, raster/push workers, animation and
  personality output. The screen is dark; the analyser continues at 62.5 Hz.
- MC_STOP / page Disconnect or a two-second PWR hold exits the mode. A lost
  browser, background-tab timer throttling or USB loss does not silently exit it.
- Nonblocking partial writes are retried; fixed queue capacity bounds memory.
  CRC32 catches damaged packets and sequence numbers identify missing frames.
- A separate lightweight behavior instance tracks whether the detector would
  enter music/listening; it cannot trigger scenes or speech.
- Raw, pre-AGC 16-band FFT powers allow per-track frequency-energy summaries and
  equal-track versus pooled-energy corpus comparisons. Old captures remain
  readable but cannot recover these raw spectra.

## Hardware observations

The first dedicated-mode build captured 6,684 consecutive frames over 106.9
seconds without heartbeats: 62.514 frames/s, zero sequence gaps, zero CRC failures,
and no renderer FPS logs during recording. Maximum reported analysis CPU time
was 596 microseconds (packing/enqueue is outside that measurement).

Closing the serial reader for six seconds and reconnecting kept session 1 active.
The queue reported 218 lost frames; sequence gaps independently reported exactly
218. Received packets passed CRC. This is an intentional overload result, not a
claim that a finite queue can record through an arbitrarily long disconnection.
MC_STOP restored ordinary expressions at approximately 60 FPS with 11,740 bytes
of render-task stack headroom. No panic or watchdog reset appeared in this check.

The final flashed build was also recorded through the live Chrome Music Lab
page: 4,306 frames over 68.9 seconds, exactly 62.50 frames/s, zero sequence gaps,
zero device drops, and a maximum reported analysis time of 359 microseconds.
The downloaded v2 notebook retained all 16 frequency bands, device configuration,
and capture-health samples; per-track and corpus frequency tables rendered in
the page. This room-noise run is a transport/UI check, not beat-detection ground
truth.

Host checks cover legacy/v2/mixed session files, CRC corruption, 16-band numerical
precision, 62.5 Hz cadence, sequence gaps, corpus aggregation, C CSV export, audio
regressions, power timing, and suppression/restoration of voice and glass effects.
The page displays capture cadence instead of treating display FPS as sample rate.

For the complete protocol, storage size and workflow, see
[Music Lab](../../tools/music-lab/README.md). These short tests are not proof of
multi-hour stability or immunity to cable/host USB faults.
