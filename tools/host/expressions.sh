#!/bin/zsh
# Actual firmware-renderer previews; run from any directory. Needs ffmpeg.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
DEST="$HERE/../../docs/expressions"
mkdir -p "$DEST"
"$HERE/build.sh" expression_preview
(cd "$HERE" && bin/expression_preview)
for name in catalog new playful knocked_out; do
  ffmpeg -nostdin -hide_banner -loglevel error -y -i "$HERE/out/character/$name.ppm" -frames:v 1 "$DEST/$name.png"
done
for name in new playful recovery transitions high_roller; do
  size=932x466
  [[ "$name" == recovery ]] && size=466x233
  [[ "$name" == transitions || "$name" == high_roller ]] && size=233x233
  ffmpeg -nostdin -hide_banner -loglevel error -y -f rawvideo -pixel_format rgb24 -video_size "$size" -framerate 20 \
    -i "$HERE/out/character/$name.rgb" -filter_complex '[0:v]split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=3' \
    -loop 0 "$DEST/$name.gif"
  ffmpeg -nostdin -hide_banner -loglevel error -y -f rawvideo -pixel_format rgb24 -video_size "$size" -framerate 20 \
    -i "$HERE/out/character/$name.rgb" -vf 'pad=ceil(iw/2)*2:ceil(ih/2)*2' -c:v libx264 -crf 20 -pix_fmt yuv420p -movflags +faststart "$DEST/$name.mp4"
done
print "Previews: $DEST"
