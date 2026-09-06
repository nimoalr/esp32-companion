#!/bin/sh
# Real behavior and renderer, scripted inputs; no audio embedded.
set -eu
cd "$(dirname "$0")/../.."
tools/host/build.sh personality_preview
tools/host/bin/personality_preview
ffmpeg -v error -y -f rawvideo -pixel_format rgb24 -video_size 699x265 -framerate 30 \
    -i tools/host/out/personality.rgb -vf 'pad=700:266' -c:v libx264 -crf 23 \
    -pix_fmt yuv420p -movflags +faststart docs/expressions/personality.mp4
ffmpeg -v error -y -ss 3 -i docs/expressions/personality.mp4 -frames:v 1 docs/expressions/personality.png
