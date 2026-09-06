#!/bin/sh
# Render the firmware's four dance shows. No third-party music is embedded.
set -eu
cd "$(dirname "$0")/../.."
tools/host/build.sh dance_preview
tools/host/bin/dance_preview
mkdir -p docs/dance
ffmpeg -v error -y -f rawvideo -pixel_format rgb24 -video_size 932x265 -framerate 30 \
    -i tools/host/out/dance.rgb -vf 'pad=932:266' -c:v libx264 -crf 23 \
    -pix_fmt yuv420p -movflags +faststart docs/dance/dance-show.mp4
ffmpeg -v error -y -ss 2.5 -i docs/dance/dance-show.mp4 -frames:v 1 docs/dance/dance-show.png
