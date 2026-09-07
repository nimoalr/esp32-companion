# Venue lighting and dance variation

[Watch the updated preview](dance-show.mp4).
Panels: **lasers / spotlights / both with spectrum / both with mirror balls**.
As before, synthetic 150 BPM music has a four-second audible breakdown and a
returning kick. The preview forces lights on for comparison; firmware uses
independent timers.

## Movement vocabulary

Pangolin describes beam-show families including fans, sheets and tunnels.
ChamSys describes phase spread across fixtures, synchronized groups, and
pan/tilt motion made from sine/cosine waveforms. Those are the references for
this small display interpretation: coherent groups of beams, offsets between
fixtures, and eased transitions instead of constantly drifting rotation.
Sources: [Pangolin beam shows](https://pangolin.com/en-cn/pages/beam-shows),
[ChamSys FX engine](https://secure.chamsys.co.uk/docs/magicq/fx-engine/FX_engine.html).

Five of seven laser looks privilege vertical orientation: parallel sweeps,
alternating up/down banks, a travelling phase wave, opposed pairs, and an
opening fan. The other two converge inward or open into a ring fan. A phrase
change every 16 detected beats selects the next look; angular transitions are
smoothed. Kicks still drive brightness and which rays are lit. Loudness and
bass control brightness, density and fan opening. Breakdowns keep a quieter
continuous sweep without manufacturing beat events.

Each show chooses its fixture positions once. Lasers choose the horizontal
middle row about two thirds of the time and eight positions around the rim
otherwise. Spotlights independently choose a row or three points around the
rim. These produce both shared arrangements and mixed arrangements, without
teleporting fixtures mid-show. The physical rigs remain aligned to the display
while the face can rotate independently.

## Independent background spotlights

Spotlights now run behind the eyes. The live eye-fill scheduler chooses plain
eyes, spectrum or mirror balls; the old eye-contained spotlight remains only
as a renderer/debug option. Three moving heads have coloured tapered beams
and elliptical pools, with phase-offset motion and independent colour choices.
Their pan/tilt targets sweep smoothly and bass changes the beam opening.

Spotlight shows last **18–36 seconds**, with **6–15 second** breaks. Their timer,
random state and fade are independent of the **30–60 second** laser shows and
**8–16 second** laser breaks. Either can appear alone or both together. The
compositor prepares an immutable snapshot at no more than 30 Hz and retains
opaque eye silhouettes over the lights. Changes include old and new bounds,
including when either layer switches off.

## Six beat moves

The dance changes its movement style on 16-beat phrase boundaries, avoiding
an immediate repeat: original bounce, lateral shuffle, alternating eye hops,
two-step sway, opposing twists, and a compressed stomp. All use the existing
measured kick envelope and tempo-scaled decay. The two-step changes the pose
pattern every two detected beats; it does not alter beat detection. Audible
breakdowns keep the existing gentle coast. Stroke flourishes remain layered
on the current movement.

## Cost and validation

The combined lighting snapshot is **14,912 bytes on the host**, including six
spotlight shapes, palettes and 24 compact laser rays. Spotlights reuse the
existing polygon/circle rasteriser; rays use sparse integer line painting.
No new task, frame-time allocation or audio analysis pass is added.

Representative host setup+raster measurements (including eye regions) are
about **107 µs lasers only, 110 µs spots only, 174 µs together** in the sampled
layouts. The rim/vertical laser arrangement repaints roughly 215k pixels per
background update: **10.77 ms ideal RGB565 transfer at 80 MHz QSPI**. Spotlights
alone average roughly 143k pixels / 7.15 ms. These figures depend on layout
and host scheduling, and are not ESP32 timing results. The larger coverage is
an explicit tradeoff for full-display beams; background changes remain capped
at 30 Hz, with intervening eye frames repainting only eye/accessory damage.
Hardware profiling remains necessary, especially with both layers and large
rotated eyes. The previous firmware already had occasional frame-rate dips
on large expressive listening poses.

UBSan compositor checks cover all eye fills with both lights, old/new damage,
opaque eye interiors, tile equality, individual ray fades, solo spotlight exit,
all four row/rim layout combinations, predominantly vertical selection, six
distinct dance poses, and independent schedules. The music admission and
breakdown regressions remain unchanged and pass.

```sh
tools/host/dance.sh
tools/host/build.sh dance_test -fsanitize=undefined -fno-sanitize-recover=all
tools/host/bin/dance_test
tools/host/build.sh dance_bench && tools/host/bin/dance_bench
```
