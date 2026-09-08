# Recent request audit

This is an implementation audit, not a claim that every behavior has been
validated by listening to or handling the device.

| Request | Status |
| --- | --- |
| More original expressions and transitions | 57 selectable animation IDs, including heart/broken-heart/reel and non-emotional authored scenes. New motion and dance-light transition fixes are in [Refinements](REFINEMENTS.md). |
| Comprehensive personality wiring | Mood-qualified cameos plus handling, recovery, affection, speech, battery/charging and accepted-word cues. [Wiring table](PERSONALITY.md). |
| Round-display/rim opportunities | All ten rim scenes are scheduled, not just demos: cautious peek, hide/relocate, too close, rim bonk, hanging on, lazy puddle, around the bend, secret observer, wrong entrance and jackpot escape. The lazy puddle now also appears during the dim phase. |
| Less accidental motion jiggle and carrying purr | Gentle gravity updates do not drive elasticity. Petting now requires four deliberate strokes; resting fingers cannot trigger purring. |
| Lower lid yields to an eye tap | Implemented, with regression coverage for the tapped and untapped eyes. |
| Longer/random dizzy, motion games and harder knockout | Games lead to 4–8 seconds of dizzy/seasick/cross-eyed animation plus sickness-dependent extension. Loose eye-shaped objects now respond to calibrated acceleration; vertical wobble can trigger reels. Knockout requires more sustained effort. |
| Repeated-tap headbutt/cracks | Four escalating stages, corrected angry lids, anger mark, synchronized recorded CC0 bonk/cracks, nine existing fracture regions falling as shards, and persistent anger. |
| Interrupt incompatible face/sound states | Active and queued purr cancellation, cleared petting qualification, actual active-speech interruption, and mood-qualified occasional listening. |
| Accurate disco balls/spotlights with eye outlines | Cached rotating disco balls inside visible eye silhouettes; moving colored spotlights behind the eyes. Independent disco cameras added. |
| Denser, long-lived lasers with independent timing | Up to 24 rays; independent 30–60 second shows. Row and rim rigs, vertical-weighted looks, fades, music response and independent spotlights. |
| More dance movement and tempo-change reactions | Six beat poses; two temporary faster-rhythm flourishes. This detects abrupt consistent faster subdivisions, not a general gradual-tempo-ramp estimator. |
| Kick sensitivity and audible breakdown retention | Implemented in the existing detector/session policy, with synthetic and limited preview evidence. Full offending tracks still need owner reference captures. |
| Efficient music calibration with local HTML coordination | Music Lab records lossless stereo microphones plus features over USB (~4.1 MB/minute), or compact features only (~240 KB/minute), with a static recording message and paused renderer. It includes raw 16-band spectra, sequence/CRC checks, local stereo replay, a zoomable range-label timeline with comments/undo, WAV/notebook export/import, and C analyser replay against human labels. It does not automatically train or tune firmware thresholds. Further tuning is pending the planned recordings. |
| More inside-eye ideas such as floating hearts | Proposed in [Music Lab/dance notes](../dance/MUSIC_LAB.md); floating-heart, liquid, fireworks and orbit fills are not implemented yet. |
| One Familiar voice for babble and reactions | Familiar words are on-device; three matching babble auditions and a reaction/purr audition exist. [Babble selection](../voice/FAMILIAR_BABBLES.md) remains pending; procedural babble/purr have not been replaced by an unchosen audition. |
| Finite unattended sleep | USB no longer prevents full sleep; unqualified onsets no longer reset inactivity; admitted music always keeps the screen active, including breakdowns; abandoned capture expires. Default is 30 seconds active, thirty minutes dim with nap/wake performances, then panel-off light sleep. |

Host coverage is broader than hardware validation. Full-track music accuracy,
worst-case combined dance rendering, physical handling feel and speaker balance
still require device/reference checks; none is inferred from a passing C test.
