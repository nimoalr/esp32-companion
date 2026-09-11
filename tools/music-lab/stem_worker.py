"""Local four-stem analysis worker. Temporary 44.1 kHz files never enter notebooks."""
import argparse
import hashlib
import importlib.metadata
import json
import math
import os
import sys
from pathlib import Path
import tempfile
import wave

RATE = 16000
HOP = 1600  # 100 ms level lanes, independent of firmware's 16 ms beat clock
STEMS = ('Drums', 'Bass', 'Vocals', 'Other')


def save_json(path, value):
    temp = path.with_suffix('.tmp')
    temp.write_text(json.dumps(value, separators=(',', ':')))
    temp.replace(path)


def db_curve(samples, hop=HOP):
    import numpy as np
    result = []
    for start in range(0, len(samples), hop):
        power = float(np.mean(np.square(samples[start:start+hop], dtype=np.float64)))
        result.append(max(-1200, min(0, round(100 * math.log10(max(power, 1e-12))))))
    return result


def core_windows(frames, seconds=60, context=2):
    size = seconds * RATE
    for start in range(0, frames, size):
        end = min(frames, start+size)
        yield start, end, max(0, start-context*RATE), min(frames, end+context*RATE)


def analyze(source, output, model_dir):
    import numpy as np
    import soundfile as sf
    import torch
    from scipy.signal import resample_poly
    from audio_separator.separator import Separator
    torch.set_num_threads(2)
    progress = output/'progress.json'
    save_json(progress, {'stage': 'Loading separation model', 'progress': 0})
    with wave.open(str(source), 'rb') as w:
        if (w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getcomptype()) != (RATE, 2, 2, 'NONE'):
            raise ValueError('Expected original 16 kHz stereo PCM16 microphone WAV')
        frames = w.getnframes()
    if not 0 < frames <= RATE*3600:
        raise ValueError('Choose a non-empty recording up to one hour long')
    with source.open('rb') as stream:
        wav_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
    levels = {name: [] for name in ('Mixture', *STEMS)}
    writers = {}
    try:
        for name in STEMS:
            w = sf.SoundFile(str(output/(name.lower()+'.flac')), 'w', samplerate=RATE,
                             channels=2, subtype='PCM_16', format='FLAC')
            writers[name] = w
        with tempfile.TemporaryDirectory(prefix='scratch-', dir=output) as temp:
            scratch = Path(temp)
            separator = Separator(output_dir=temp, model_file_dir=str(model_dir), use_soundfile=True,
                                  normalization_threshold=1., amplification_threshold=0.,
                                  demucs_params={'segment_size': 6, 'shifts': 0, 'overlap': .1, 'segments_enabled': True})
            separator.load_model(model_filename='htdemucs.yaml')
            windows = list(core_windows(frames))
            for i, (start, end, left, right) in enumerate(windows):
                save_json(progress, {'stage': f'Separating passage {i+1} of {len(windows)}', 'progress': start/frames})
                with wave.open(str(source), 'rb') as w:
                    w.setpos(left)
                    raw = w.readframes(right-left)
                x = np.frombuffer(raw, dtype='<i2').reshape(-1, 2).astype(np.float32)/32768
                chunk = scratch/'passage.wav'
                sf.write(chunk, x, RATE, subtype='PCM_16')
                names = separator.separate(str(chunk))
                found = {}
                for name in names:
                    stem = next((s for s in STEMS if f'_({s})_' in name), None)
                    if stem is None or stem in found:
                        raise ValueError(f'Unexpected stem output: {name}')
                    y, rate = sf.read(scratch/name, always_2d=True, dtype='float32')
                    if y.shape[1] != 2 or not np.isfinite(y).all() or abs(len(y)/rate-len(x)/RATE) > .05:
                        raise ValueError('Separated audio has invalid channels, samples or duration')
                    divisor = math.gcd(RATE, rate)
                    if rate != RATE:
                        y = resample_poly(y, RATE//divisor, rate//divisor, axis=0)
                    offset = start-left
                    core = y[offset:offset+end-start]
                    if len(core) != end-start:
                        raise ValueError('Separation did not cover the requested microphone samples')
                    # Quantize once; levels describe exactly the auditioned PCM.
                    pcm = np.clip(np.rint(core*32768), -32768, 32767).astype('<i2')
                    writers[stem].write(pcm)
                    found[stem] = db_curve(pcm.astype(np.float32)/32768)
                    (scratch/name).unlink()
                if set(found) != set(STEMS):
                    raise ValueError('The model did not produce all four stems')
                core = x[start-left:end-left]
                levels['Mixture'].extend(db_curve(core))
                for stem in STEMS:
                    levels[stem].extend(found[stem])
    finally:
        for w in writers.values():
            w.close()
    reference = {'format': 'music-lab-stems-v1', 'source': {'wavSHA256': wav_hash,
                 'sampleRate': RATE, 'channels': 2, 'frames': frames},
                 'model': 'htdemucs.yaml', 'separatorVersion': importlib.metadata.version('audio-separator'),
                 'torchVersion': torch.__version__, 'hopSamples': HOP, 'unit': '0.1 dBFS',
                 'levels': levels, 'contextSeconds': 2, 'chunkSeconds': 60,
                 'note': 'Model estimates, not human labels. Vocals can include speech. Stem energies need not sum to mixture energy; missing audio remains missing.'}
    save_json(output/'reference.json', reference)
    save_json(progress, {'stage': 'Ready', 'progress': 1})
    return reference


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path)
    ap.add_argument('output', type=Path)
    ap.add_argument('--model-dir', type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    if hasattr(os, 'nice'):
        os.nice(10)
    analyze(args.source, args.output, args.model_dir)
    # All WAVs and atomic JSON outputs are closed by analyze(). On macOS the
    # optional ML runtime can abort in C++ global destructors after successful
    # inference (recursive_mutex lock failed). This one-job subprocess has no
    # remaining Python cleanup to perform. Failures above still exit nonzero.
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(0)
