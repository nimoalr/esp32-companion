#!/bin/sh
# Run from any directory. Source excerpts and CC0 provenance are committed.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p tools/host/out/glass
fx='highpass=f=380,lowpass=f=6800,equalizer=f=1500:t=o:w=1.5:g=4'
ffmpeg -v error -y -i tools/host/assets/glass/knock_excerpt.wav -af "$fx,afade=t=in:d=0.002,afade=t=out:st=0.35:d=0.04" -f s16le tools/host/out/glass/bonk.pcm
ffmpeg -v error -y -i tools/host/assets/glass/break_excerpt.wav -t 0.28 -af "$fx,afade=t=in:d=0.001,afade=t=out:st=0.22:d=0.06" -f s16le tools/host/out/glass/crack.pcm
ffmpeg -v error -y -i tools/host/assets/glass/break_excerpt.wav -af "$fx,afade=t=in:d=0.001,afade=t=out:st=0.98:d=0.10" -f s16le tools/host/out/glass/shatter.pcm
ffmpeg -v error -y -ss 0.38 -t 0.3 -i tools/host/assets/glass/break_excerpt.wav -af "$fx,afade=t=in:d=0.004,afade=t=out:st=0.24:d=0.06" -f s16le tools/host/out/glass/tinkle.pcm
tools/host/build.sh sfx_pack
tools/host/bin/sfx_pack
tools/host/build.sh glass_sounds
tools/host/bin/glass_sounds
