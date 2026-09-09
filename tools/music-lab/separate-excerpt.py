#!/usr/bin/env python3
"""Optional local ML experiment, separate from dependency-free capture/replay.

Install in a Python 3.12 venv: audio-separator[cpu]==0.47.0 audioread==3.1.0
Pretrained weights download on first use; input audio is processed locally.
"""
import argparse
import hashlib
import importlib.metadata
import json
import math
from pathlib import Path
import subprocess
import time


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input', type=Path, help='Exported stereo microphone WAV')
    ap.add_argument('output', type=Path, help='New directory for this experiment')
    ap.add_argument('--start', type=float, default=0)
    ap.add_argument('--seconds', type=float, default=12, help='1–60 seconds; default 12')
    ap.add_argument('--model', default='htdemucs.yaml', help='Audio Separator model filename')
    args = ap.parse_args()
    if not math.isfinite(args.start) or args.start < 0 or not 1 <= args.seconds <= 60:
        ap.error('Use a finite nonnegative start and a duration of 1–60 seconds')
    if not args.input.is_file():
        ap.error('Input WAV does not exist')
    args.output.mkdir(parents=True, exist_ok=False)
    excerpt = args.output / 'microphones.wav'
    subprocess.run(['ffmpeg', '-v', 'error', '-ss', str(args.start), '-t', str(args.seconds),
                    '-i', str(args.input.resolve()), '-ar', '16000', '-ac', '2',
                    '-c:a', 'pcm_s16le', str(excerpt)], check=True)

    # Heavy dependencies are confined to this optional host tool.
    import numpy as np
    import soundfile as sf
    import torch
    from audio_separator.separator import Separator
    torch.set_num_threads(4)
    original, rate = sf.read(excerpt, always_2d=True)
    if not len(original):
        raise ValueError('Selected excerpt is empty')
    reference = float(np.mean(original * original))
    separator = Separator(output_dir=str(args.output), use_soundfile=True,
                          normalization_threshold=1.0, amplification_threshold=0.0,
                          demucs_params={'segment_size': 6, 'shifts': 0, 'overlap': .1,
                                         'segments_enabled': True})
    separator.load_model(model_filename=args.model)
    start = time.monotonic()
    outputs = separator.separate(str(excerpt))
    elapsed = time.monotonic() - start
    stems = []
    for name in outputs:
        output = args.output / name
        y, sr = sf.read(output, always_2d=True)
        if not len(y) or not np.isfinite(y).all():
            raise ValueError(f'Invalid separated output: {name}')
        if abs(len(y) / sr - len(original) / rate) > .05:
            raise ValueError(f'Stem duration does not match excerpt: {name}')
        energy = float(np.mean(y * y))
        stems.append({'file': name, 'sampleRate': sr, 'channels': y.shape[1],
                      'duration': len(y) / sr, 'rms': math.sqrt(energy),
                      'energyDbRelativeToMixture': 10 * math.log10(energy / reference)
                      if energy > 0 and reference > 0 else None})
    manifest = {
        'source': str(args.input.resolve()), 'startSeconds': args.start,
        'durationSeconds': len(original) / rate,
        'excerptSHA256': hashlib.sha256(excerpt.read_bytes()).hexdigest(),
        'model': args.model, 'separatorVersion': importlib.metadata.version('audio-separator'),
        'torchVersion': torch.__version__, 'inferenceSeconds': elapsed, 'stems': stems,
        'note': 'Local model estimates, not human labels. Stem energies need not add to mixture energy. '
                'Model resampling cannot recover content missing from the 16 kHz microphones.',
    }
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
