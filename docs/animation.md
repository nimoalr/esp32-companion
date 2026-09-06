# How the eyes are animated

Two orange blobs on a black disc can read as alive or as a screensaver. The
difference is almost entirely in the secondary motion, and the best reference
for it is the procedural face of Anki's Cozmo and Vector: a pair of rounded
rectangles, no pupils, no brows, animated by a Pixar animator (Carlos Baena)
and later released as open source. This file lists what was learned from that
source and from the literature, and how each point is implemented here.

## Sources

* Anki Vector source, `cannedAnimLib/proceduralFace/` and
  `animProcess/.../faceLayerManager.cpp`
  (github.com/digital-dream-labs/vector): the eye parameter set, the
  `KeepFaceAlive` idle behaviour, the blink and dart keyframes.
* Anki Vector technical reference, "Procedural face" chapter (hotspot and
  glow description, parameter ranges).
* Lee, Badler & Badler, *Eyes Alive*, SIGGRAPH 2002: statistical saccade
  model (duration about 2.2 ms per degree plus 21 ms, inter-saccade intervals).
* Blink physiology summaries: 16-20 blinks/min at rest (3-30 depending on
  the task), closing about twice as fast as reopening, blink probability
  rising with saccade amplitude (gaze-evoked blinks).
* Community eye libraries for small displays: FluxGarage RoboEyes
  (curiosity mode, idle mode, autoblinker), ggldnl Procedural-Expression-Library
  (polygon interpolation), esp32-eyes.

## What Vector's eyes can do

Per eye: centre, scale X/Y, angle, four corner radii with independent X and
Y, upper and lower lid height, lid angle and lid *bend* (a curved lid),
saturation, lightness, a glow and a hot spot that moves with the gaze. Per
face: angle, centre, scale, scanline opacity. Nominal eye 43 x 57 px on a
184 x 96 px screen, so proportions below are scaled by about 2.5 for the
466 px disc.

Idle (`KeepFaceAlive`):

* Eye darts every 1000-2250 ms, up to 15 px from the centre (1 px when the
  robot is focusing on something). Darts of more than 5 px insert an
  in-between frame with the eyes squashed to 0.85 height; darts of more than
  10 px insert two frames at 0.7 and 0.85 (a "mini blink"). The axis that
  moves less lags behind (shift-lag 0.4), so the path curves instead of going
  straight.
* Looking up scales the eyes by 1.05, looking down by 0.9. Looking sideways
  makes the leading eye 3 % taller.
* Blinks every 3000-10000 ms. The blink is asymmetric and the eye widens as
  it thins (height x width per 33 ms frame): 0.85 x 1.05, 0.6 x 1.2, 0.1 x 2.5,
  0.05 x 5.0 (closed, lids zeroed, both eyes on the average height), 0.15 x
  2.0, 0.7 x 1.2, 0.9 x 1.0 for 100 ms, then the pose is restored. Closing
  takes 100 ms, opening about 170 ms.
* Squint: scale Y 0.35, scale X 1.05, upper lid angle -10 deg, over 250 ms.

## What is implemented here

Shape (`raster.c`, `eyes.h`):

* Four independent corner radii per eye (`rad[4]`, TL/TR/BL/BR, as a scale of
  the base radius). Flat-bottomed happy arcs, sharp inner corners for anger,
  rounder eyes for surprise and love.
* A bend on the top lid (`bend`, fraction of the eye height): positive
  droops into the eye (sleepy, bored, sad), negative arches it up
  (skeptical, scared).
* A per-eye angle (`angle`, degrees) on top of the face rotation: inner
  corners down for anger, outer corners down for sadness, one tilted eye for
  confusion, a counter-phase wobble for dizziness.
* Everything stays analytic: the upright path is integer Q16, with one
  square root per sub-scanline when the row's two corners share a radius and
  two when they do not; a bent lid or an angled eye switches to the float
  per-sub-row path already used for rotated faces (a quadratic per lid).

Secondary motion (`eyes.c`):

* Blink table with Vector's timing and a width gain capped at 1.32 (5x would
  put the two eyes into each other at this spacing), linearly interpolated at
  60 fps; the lids, the arc and the bend fade out as the eye closes, the
  sliver sits where the visible part of the eye was, and both eyes meet on
  their average height. Interval 3-10 s, scaled per expression.
* Gaze darts every 1-2.25 s, up to 16 x 11 px, duration 40 ms + 3 ms/px.
  Medium darts squash the eyes 15 %, long darts 28 %, peaking a third of the
  way through; the lagging axis waits for the first 30 % so the path bends.
  Long darts trigger a blink 30 % of the time. Per-expression amplitude
  scale (none while sleeping or dizzy, more when scared or excited).
* Gaze-dependent size: +6 % looking up, -8 % looking down, the leading eye
  4.5 % taller looking sideways, over a 24 px range.
* Stretch along fast pose-driven moves and squash across them, from the
  centre velocity (max 18 %, fast attack, slow release).
* Two easings for expression changes: smoothstep, or `SNAP`, an ease-out
  with a 6 % overshoot on size and position for the expressive changes
  (happy, surprised, excited, angry, love, scared, laughing, curious).
  SURPRISED first squashes to 0.96 x 0.92 for 60 ms (anticipation), then
  pops.

Colour (`eyes.c`, `anim.c`, `settings.c`):

* Vector's eye colour is a user preference (seven named hue/saturation
  pairs in `eye_color_config.json`; the TRM lists them) and stays the
  robot's identity: animations never change the hue, they only scale a
  per-eye Lightness (and, behind a compile flag, Saturation), and the whole
  face is rendered as a value image converted to RGB565 through the chosen
  hue and saturation. Dim eyes mean low power or sleep, bright eyes mean
  attention.
* Here the base colour is a setting (eight named colours in the setup UI,
  stored in NVS; the UI's accent follows it). Each expression carries a tint:
  a small relative hue shift, a blend toward a fixed colour in RGB space
  (anger 30 % toward red, love 35 % toward pink, dizzy 30 % toward a sickly
  green; RGB rather than hue so "toward red" means the same on a teal base as
  on an orange one), and saturation and lightness multipliers (sad 0.70 x
  0.78, scared pale at 0.55 saturation, sleeping 0.45 lightness, happy and
  excited a little brighter). Tints cross-fade over 450 ms with the pose.
* Mood scales lightness (0.85 to 1.0) and saturation (0.90 to 1.0) with the
  character's energy, so a tired character is dimmer and paler. The dance
  choreography flashes the lightness 22 % on each beat and shimmers the hue
  by 8 deg with the bass.
* The colour is one HSL to RGB conversion per frame and a 256-entry LUT
  rebuild only when the 8-bit result changes; the rasteriser is untouched.

Second round, after the first pass proved cheap (`raster.c`, `eyes.c`):

* **Hot spot** (always on): a Gaussian lightness falloff with
  sigma = 0.5 x eye height, centred 1.5 x the gaze offset ahead of the eye
  centre (Vector's `HotSpotPositionMultiplier`), darkest level 0.55 at
  infinity, so the eye edge sits near 0.87. Separable, so it is two per-axis
  tables per eye and one multiply per pixel; a 2x2 ordered dither hides the
  8-bit steps in RGB565. No outer glow.
* **Elliptical corners**: independent x and y radius per corner, so LOVE and
  CURIOUS get tall soft ellipses and SLEEPY gets wide shallow bottoms.
  Circular corners keep the cheaper unit quadratic in the rotated path.
* **Lower lid slant and bend**: the bottom lid has the same controls as the
  top one; a negative bend lets the lid sag in the middle (SAD and SHY pout),
  ANGRY pinches from below as well as above.
* **Saturation and lightness per eye**: WINK dims the closed eye, CURIOUS and
  SKEPTICAL brighten the open eye and dim the squinting one.
* **Face-level centre and scale**: both eyes placed and scaled about the
  screen centre; the dance pulses the whole face with the bass, SLEEPING
  breathes with it, EXCITED bounces it.
* **Focus mode**: a finger resting on the screen. The gaze eases toward the
  touched point (0.11 px of eye travel per px of screen distance, capped at
  24 x 18 px), the eyes open 4 % and the idle darts shrink to 7 % of their
  amplitude, Vector's 15 px to 1 px. Everything else (blinks, dart timing,
  squash, gaze scaling) keeps running, which is what makes it read as
  attention rather than a freeze.
* **Squint** as a selectable expression.

Still not taken: the outer glow, scanline opacity and noise (CRT mimicry
that would read as a defect on the AMOLED), and the vision-driven parts
(look-at targets, eye contact).
