#!/usr/bin/env python3
"""Compare estimated stem levels with human ranges and an aligned firmware replay.

100 ms stem levels are descriptive estimates, not beat annotations or labels.
No classifier is fitted and no firmware parameters are changed.
"""
import argparse
import csv
import json
import math
from pathlib import Path
from importlib import import_module
import hashlib
import wave

validate = import_module('stem-corpus').validate
NAMES = ('Mixture', 'Drums', 'Bass', 'Vocals', 'Other')


def overlap(a, b, c, d):
    return max(0., min(b, d)-max(a, c))


def range_levels(reference, start, end):
    duration = reference['source']['frames']/16000
    weights = [(i, overlap(start, end, i*.1, min(duration, (i+1)*.1)))
               for i in range(max(0, math.floor(start*10)), min(len(reference['levels']['Mixture']), math.ceil(end*10)))]
    seconds = sum(w for _, w in weights)
    if not seconds:
        raise ValueError('Empty analysis range')
    power = {name: sum(w*10**(reference['levels'][name][i]/100) for i, w in weights)/seconds for name in NAMES}
    return {'mixtureDbFS': 10*math.log10(power['Mixture']),
            'relativeDb': {n: 10*math.log10(power[n]/power['Mixture']) for n in NAMES[1:]}}


def range_replay(rows, start, end):
    weights = [(r, overlap(start, end, i*.016, (i+1)*.016))
               for i, r in enumerate(rows) if i*.016 < end and (i+1)*.016 > start]
    seconds = sum(w for _, w in weights)
    mean = lambda key: sum(r[key]*w for r, w in weights)/seconds
    return {'dancePercent': 100*mean('would_dance'), 'meanDrive': mean('dance_drive'),
            'speechFlagPercent': 100*mean('speech'), 'meanRhythmCorrelation': mean('music_conf'),
            'rhythmEvidenceReadyPercent': 100*sum(w for r, w in weights if r['music_evidence'] >= 1.5)/seconds,
            'aboveLoudness80Percent': 100*sum(w for r, w in weights if r['raw_loud'] > 80)/seconds}


def positive_parts(labels, duration):
    # A union of explicit positives, excluding conflicting explicit negatives.
    edges = sorted({0., duration, *(max(0., min(duration, l[k])) for l in labels for k in ('start', 'end'))})
    return [(a, b) for a, b in zip(edges, edges[1:]) if b > a and
            {l['expected'] for l in labels if l['expected'] != 'unsure' and l['start'] < b and l['end'] > a} == {'dance'}]


def missed_profile(reference, rows, parts):
    result = {'labelledSeconds': 0., 'missedSeconds': 0.,
              'missedWithDrumsBelowRelativeDb': {str(t): 0. for t in (-15, -20, -25)}}
    for i, row in enumerate(rows):
        a, b = i*.016, (i+1)*.016
        seconds = sum(overlap(a, b, c, d) for c, d in parts)
        result['labelledSeconds'] += seconds
        if not seconds or row['would_dance']:
            continue
        result['missedSeconds'] += seconds
        # Split a replay frame that straddles two level bins; don't shift clocks.
        for j in range(int(a*10), min(len(reference['levels']['Mixture']), math.ceil(b*10))):
            weight = sum(overlap(max(a, j*.1), min(b, (j+1)*.1), c, d) for c, d in parts)
            relative = (reference['levels']['Drums'][j]-reference['levels']['Mixture'][j])/10
            for threshold in result['missedWithDrumsBelowRelativeDb']:
                if relative < float(threshold):
                    result['missedWithDrumsBelowRelativeDb'][threshold] += weight
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('exports', type=Path)
    ap.add_argument('references', type=Path)
    args = ap.parse_args()
    tracks = []
    for path in sorted(args.exports.glob('[0-9][0-9][0-9].wav')):
        meta = json.loads(path.with_suffix('.json').read_text())
        if meta.get('gaps'):
            raise ValueError('This report requires gap-free exports; exclude missing-audio spans before analysis')
        reference = json.loads((args.references/(path.stem+'-reference.json')).read_text())
        with wave.open(str(path)) as w:
            frames = w.getnframes()
        validate(reference, hashlib.sha256(path.read_bytes()).hexdigest(), frames)
        with (args.exports/(path.stem+'-candidate.csv')).open() as f:
            rows = [{k: float(v) for k, v in row.items()} for row in csv.DictReader(f)]
        if len(rows)*256 != frames or any(r['time_ms'] != i*16 for i, r in enumerate(rows)):
            raise ValueError('Firmware replay does not cover the exact microphone timeline')
        duration = frames/16000
        if any(not all(math.isfinite(v) for v in row.values()) or row['would_dance'] not in (0, 1) for row in rows):
            raise ValueError('Invalid replay values')
        labels = meta.get('annotations', [])
        for label in labels:
            if not 0 <= label['start'] < label['end'] <= duration or label['expected'] not in ('dance', 'no_dance', 'unsure'):
                raise ValueError('Invalid annotation')
        tracks.append({'index': path.stem, 'track': meta['track'], 'kind': meta['kind'],
                       'duration': duration, 'sourceSHA256': reference['source']['wavSHA256'],
                       'wholeTrack': {**range_levels(reference, 0, duration), **range_replay(rows, 0, duration)},
                       'annotations': [{**l, **range_levels(reference, l['start'], l['end']),
                                        **range_replay(rows, l['start'], l['end'])} for l in labels],
                       'positiveMisses': missed_profile(reference, rows, positive_parts(labels, duration))})
    print(json.dumps({'note': 'Model estimates on a tuning corpus. Individual annotations may overlap; '
                     'positiveMisses uses their union and excludes unsure/conflicting ranges. '
                     'Drum-level cutoffs are descriptive sensitivity probes, not absence labels. '
                     'Relative energies need not add to 100%. Singing is not distinguished from speech.',
                     'tracks': tracks}, indent=2))


if __name__ == '__main__':
    main()
