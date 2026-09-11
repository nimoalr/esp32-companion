#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
tools/host/build.sh play_preview
tools/host/bin/play_preview
ffmpeg -v error -y -f rawvideo -pixel_format rgb24 -video_size 699x530 -framerate 30 \
    -i tools/host/out/play.rgb -vf 'pad=700:530' -c:v libx264 -crf 23 \
    -pix_fmt yuv420p -movflags +faststart docs/expressions/play.mp4
ffmpeg -v error -y -ss 3.65 -i docs/expressions/play.mp4 -frames:v 1 docs/expressions/play.png
ffmpeg -v error -y -ss 9.6 -i docs/expressions/play.mp4 -frames:v 1 -vf "crop=232:264:466:266" docs/expressions/headbutt.png
