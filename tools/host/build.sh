#!/bin/sh
# Build one host harness against the firmware sources (no ESP-IDF needed).
#   tools/host/build.sh <name> [extra cflags]      -> tools/host/bin/<name>
# Harnesses render PPM files into tools/host/out/; tile them with tile.py.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
R="$HERE/../../main"
name=$1
shift || { echo "usage: build.sh <harness> [cflags]"; exit 1; }
SRCS="$R/raster.c $R/eyes.c $R/eye_symbols.c $R/dance_fill.c $R/dance_lasers.c $R/anim.c $R/play.c $R/glass.c $R/sfx.c $R/adpcm.c $R/gfx.c $R/font_spleen_8x16.c $R/font_spleen_12x24.c $R/font_spleen_16x32.c $R/accessories.c $R/behavior.c $R/imu_cal.c $R/rhythm_rush.c"
case "$name" in
    glass_sounds) SRCS="$R/sfx.c $R/adpcm.c" ;;
    bar_runs_test) SRCS="" ;;
    charge_cache) SRCS="" ;;
    sfx_pack) SRCS="$R/adpcm.c" ;;
    speech_test) SRCS="$R/voice.c $R/sfx.c $R/adpcm.c $R/clips_gen.c" ;;
    interaction_test) SRCS="$SRCS $R/persona.c" ;;
    ui_harness) SRCS="$SRCS $R/ui.c" ;;
    imu_cal_test) SRCS="$R/imu_cal.c" ;;
    micdir_test) SRCS="$R/micdir.c" ;;
    voice_render) SRCS="$R/voice.c" ;;
    audio_test|rush_test) SRCS="$R/micdir.c $R/behavior.c $R/imu_cal.c $R/rhythm_rush.c" ;;
    audio_replay) SRCS="$R/micdir.c $R/behavior.c $R/imu_cal.c $R/rhythm_rush.c" ;;
    trace_test) SRCS="$R/music_trace.c" ;;
    robot|music_report) SRCS="" ;;
esac
src="$HERE/$name.c"
[ -f "$src" ] || src="$HERE/drafts/$name.c"
mkdir -p "$HERE/bin" "$HERE/out"
${CC:-gcc} -O2 -g -Wall -I"$HERE/stubs" -I"$R" "$@" "$src" $SRCS -lm -o "$HERE/bin/$name"
echo "built $HERE/bin/$name"
