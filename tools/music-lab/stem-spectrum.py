#!/usr/bin/env python3
"""Describe frequency energy in a PCM stem; this does not establish kick times."""
import argparse
import json
import math
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('audio', type=Path, help='PCM WAV or lossless FLAC stem')
    ap.add_argument('--start', type=float, default=0)
    ap.add_argument('--end', type=float, required=True)
    args = ap.parse_args()
    import numpy as np
    import soundfile as sf
    from scipy.signal import welch
    with sf.SoundFile(str(args.audio)) as w:
        if (w.samplerate, w.channels, w.subtype) != (16000, 2, 'PCM_16'):
            raise ValueError('Expected stereo 16 kHz PCM16')
        if not math.isfinite(args.start) or not math.isfinite(args.end) or not 0 <= args.start < args.end <= len(w)/16000:
            raise ValueError('Invalid range')
        start, end = round(args.start*16000), round(args.end*16000)
        if end-start < 2048:
            raise ValueError('Choose at least 128 ms')
        w.seek(start)
        samples = w.read(end-start, dtype='float64', always_2d=True)
    frequencies, power = welch(samples, 16000, nperseg=2048, noverlap=1024, axis=0)
    power = power.mean(axis=1)
    total = float(power.sum())
    edges = [0, 80, 160, 320, 640, 2000, 8001]
    bands = [float(power[(frequencies >= a) & (frequencies < b)].sum()/total*100)
             if total else 0 for a, b in zip(edges, edges[1:])]
    print(json.dumps({'start': start/16000, 'end': end/16000, 'bandEdgesHz': edges,
                      'energyPercent': bands, 'method': 'Welch, 2048 samples, Hann window, 50% overlap, '
                      'mean stereo power. Bin boundaries approximate edges; 8001 includes Nyquist. '
                      'Stem estimates and frequency energy are not kick/snare timing labels.'}, indent=2))


if __name__ == '__main__':
    main()
