#!/usr/bin/env python3
"""Populate Music Lab stems for an exported corpus through its local job queue.

Outputs compact reference JSONs for analysis and notebook attachment. Original
PCM and annotations are never modified; existing output references are validated.
"""
import argparse
import hashlib
import json
from pathlib import Path
import time
from urllib.request import Request, urlopen
import wave


def request(base, suffix='', body=None, method=None):
    req = Request(base+'/api/stems'+suffix, data=body,
                  headers={'Content-Type': 'audio/wav'} if body else {}, method=method)
    with urlopen(req, timeout=120) as response:
        return json.load(response)


def validate(reference, digest, frames):
    source = reference['source']
    if reference.get('format') != 'music-lab-stems-v1' or source != {
            'wavSHA256': digest, 'sampleRate': 16000, 'channels': 2, 'frames': frames}:
        raise ValueError('Reference identity does not match microphone WAV')
    if reference['hopSamples'] != 1600 or reference['unit'] != '0.1 dBFS':
        raise ValueError('Unexpected level timebase or units')
    for name in ('Mixture', 'Drums', 'Bass', 'Vocals', 'Other'):
        values = reference['levels'][name]
        if len(values) != (frames+1599)//1600 or any(type(x) is not int or not -1200 <= x <= 0 for x in values):
            raise ValueError('Invalid level curve')


def validate_flac(data, frames):
    if len(data)<42 or data[:4]!=b'fLaC' or data[4]&127 or int.from_bytes(data[5:8],'big')!=34:
        raise ValueError('Invalid FLAC stem')
    value=int.from_bytes(data[18:26],'big')
    if (value>>44, ((value>>41)&7)+1, ((value>>36)&31)+1, value&((1<<36)-1)) != (16000,2,16,frames):
        raise ValueError('FLAC sample timeline mismatch')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('exports', type=Path)
    ap.add_argument('output', type=Path)
    ap.add_argument('--port', type=int, default=8765)
    args = ap.parse_args()
    base = f'http://127.0.0.1:{args.port}'
    args.output.mkdir(parents=True, exist_ok=True)
    for source in sorted(args.exports.glob('[0-9][0-9][0-9].wav')):
        data = source.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        with wave.open(str(source)) as wav:
            if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) != (16000, 2, 2):
                raise ValueError('Expected stereo 16 kHz PCM16')
            frames = wav.getnframes()
        metadata = json.loads(source.with_suffix('.json').read_text())
        print(f'{source.stem}: {metadata["track"]} ({frames/16000:.1f}s)', flush=True)
        existing=args.output/(source.stem+'-reference.json')
        if existing.exists() and all((args.output/(source.stem+'-'+s+'.flac')).exists() for s in ('drums','bass','vocals','other')):
            validate(json.loads(existing.read_text()),digest,frames)
            for stem in ('drums','bass','vocals','other'):
                validate_flac((args.output/(source.stem+'-'+stem+'.flac')).read_bytes(),frames)
            print('  existing local export verified',flush=True)
            continue
        # The server arbitrates with UI jobs; never cancel somebody else's work.
        state = request(base, '/'+digest)
        while state['state'] not in ('ready', 'running'):
            capabilities = request(base)
            if not capabilities['available']:
                raise RuntimeError('Run setup-ml.sh and restart the local server')
            if capabilities['active']:
                time.sleep(2)
                state = request(base, '/'+digest)
                continue
            state = request(base, body=data)
        previous = None
        while state['state'] == 'running':
            stage = state.get('stage', 'Starting')
            if stage != previous:
                print('  '+stage, flush=True)
                previous = stage
            time.sleep(2)
            state = request(base, '/'+digest)
        if state['state'] != 'ready':
            raise RuntimeError(state.get('error', 'Stem job interrupted'))
        reference = state['reference']
        validate(reference, digest, frames)
        try:
            for stem in ('drums','bass','vocals','other'):
                with urlopen(base+'/api/stems/'+digest+'/'+stem+'.flac',timeout=120) as response:
                    data=response.read(128000001)
                if len(data)>128000000:
                    raise ValueError('FLAC size limit exceeded')
                validate_flac(data,frames)
                target=args.output/(source.stem+'-'+stem+'.flac')
                temp=target.with_suffix('.tmp');temp.write_bytes(data);temp.replace(target)
        finally:
            request(base,'/'+digest,method='DELETE')
        target = args.output/(source.stem+'-reference.json')
        temp = target.with_suffix('.tmp')
        temp.write_text(json.dumps(reference, separators=(',', ':')))
        temp.replace(target)
        print('  reference and FLACs received; temporary server files removed', flush=True)


if __name__ == '__main__':
    main()
