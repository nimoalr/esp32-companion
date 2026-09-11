# Latest verified firmware deployment

On 2026-09-11, built and flashed clean revision **d43511a** onto the USB-connected
ESP32-S3 using ESP-IDF 5.5.5 and `/dev/cu.usbmodem2101`.
This includes the annotation-based music detector and gentle dance changes from
`7f42bcf`. Subsequent stem/speech investigations are analysis only; they did not
implement the proposed speech-led versus music-led classifier.

Full `idf.py flash` updated bootloader, partition table and application, with all
write hashes verified. No NVS erase was performed. Boot reported app version
`d43511a`, the 4 MiB factory partition, and loaded existing accelerometer and
microphone calibrations. Both ES7210 microphones opened at 16 kHz stereo, and the
ES8311 playback codec opened successfully. Runtime output continued through about
22 seconds without a crash or reset; sampled audio CPU readings were roughly
0.29–0.51 ms/frame. This is a startup smoke check, not a dance-performance or
car-detection benchmark. The serial monitor was closed after verification.

Application size: 836,352 bytes (`0xcc300`), leaving about 80% of the 4 MiB app
partition free. Boot also reported a flash chip larger than the configured 16 MiB
and an already-installed GPIO ISR service; startup continued. No hardware layout
or ISR behavior was changed as part of this deployment.

Earlier reports saying “not flashed” describe their original analysis date;
this record establishes the later deployment.
