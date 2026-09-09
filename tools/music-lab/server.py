#!/usr/bin/env python3
"""Music Lab static server plus an optional local, single-job stem worker."""
import argparse
import hashlib
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
from urllib.parse import urlparse
import uuid
import wave

ROOT = Path(__file__).resolve().parent
MAX_UPLOAD = 128_000_000
CACHE_LIMIT = 1_000_000_000
STEMS = {'drums', 'bass', 'vocals', 'other'}


class Jobs:
    def __init__(self, cache, python):
        self.cache, self.python = cache, python
        cache.mkdir(parents=True, exist_ok=True)
        self.lock = threading.RLock()
        self.active = None
        self.process = None
        self.cancelled = set()
        self.available = False
        if Path(python).is_file():
            check = subprocess.run([python, '-c', "import importlib.util;raise SystemExit(importlib.util.find_spec('audio_separator') is None)"], capture_output=True)
            self.available = check.returncode == 0

    def status(self, key):
        folder = self.cache/key
        if key == self.active:
            p = folder/'progress.json'
            state = json.loads(p.read_text()) if p.exists() else {'stage': 'Starting local analysis', 'progress': 0}
            return {'id': key, 'state': 'running', **state}
        if (folder/'reference.json').is_file():
            return {'id': key, 'state': 'ready', 'progress': 1, 'reference': json.loads((folder/'reference.json').read_text())}
        if (folder/'error.txt').is_file():
            return {'id': key, 'state': 'failed', 'error': (folder/'error.txt').read_text()}
        return {'id': key, 'state': 'missing'}

    def submit(self, source):
        digest = hashlib.sha256()
        with source.open('rb') as f:
            for block in iter(lambda: f.read(65536), b''):
                digest.update(block)
        key = digest.hexdigest()
        with self.lock:
            if not self.available:
                raise ValueError('Install local analysis first: run tools/music-lab/setup-ml.sh, then restart Music Lab.')
            if self.status(key)['state'] == 'ready' or self.active == key:
                os.utime(self.cache/key, None)
                source.unlink()
                return self.status(key)
            if self.active:
                raise ValueError('Another track is being analysed. Wait for it or cancel it first.')
            with wave.open(str(source), 'rb') as w:
                if (w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getcomptype()) != (16000, 2, 2, 'NONE') or not 0 < w.getnframes() <= 16000*3600:
                    raise ValueError('Expected a non-empty 16 kHz stereo PCM16 recording, up to one hour')
                if w.getnframes()*4 > source.stat().st_size:
                    raise ValueError('Incomplete microphone WAV')
            # Bound only generated cache files, never notebooks or user recordings.
            size = lambda p: sum(f.stat().st_size for f in p.rglob('*') if f.is_file())
            folders = sorted((p for p in self.cache.iterdir() if p.is_dir() and re.fullmatch('[a-f0-9]{64}', p.name)), key=lambda p:p.stat().st_mtime)
            total = sum(size(p) for p in folders)
            reserve = source.stat().st_size*5
            for old in folders:
                if total+reserve <= CACHE_LIMIT:
                    break
                total -= size(old)
                shutil.rmtree(old)
            folder = self.cache/key
            if folder.exists():
                shutil.rmtree(folder)
            folder.mkdir()
            source.replace(folder/'source.wav')
            self.active = key
            self.cancelled.discard(key)
            threading.Thread(target=self.run, args=(key,), daemon=True).start()
            return self.status(key)

    def run(self, key):
        folder = self.cache/key
        with (folder/'worker.log').open('w') as log:
            try:
                with self.lock:
                    if key in self.cancelled:
                        raise RuntimeError('Analysis cancelled')
                    self.process = subprocess.Popen([self.python, str(ROOT/'stem_worker.py'), str(folder/'source.wav'), str(folder), '--model-dir', str(self.cache.parent/'models')], stdout=log, stderr=log)
                code = self.process.wait()
                if code or key in self.cancelled or not (folder/'reference.json').exists():
                    raise RuntimeError('Analysis cancelled' if key in self.cancelled else 'Local separation failed. Check the Music Lab server log or worker.log in its cache; your recording is unchanged.')
            except Exception as e:
                (folder/'error.txt').write_text(str(e))
                for name in STEMS:
                    (folder/(name+'.wav')).unlink(missing_ok=True)
                (folder/'reference.json').unlink(missing_ok=True)
                print(f'Stem analysis {key[:8]}: {e}; details: {folder}/worker.log', file=sys.stderr)
            finally:
                (folder/'source.wav').unlink(missing_ok=True)
                with self.lock:
                    self.process = None
                    self.active = None

    def cancel(self, key):
        with self.lock:
            if self.active == key:
                self.cancelled.add(key)
                if self.process:
                    process = self.process
                    process.terminate()
                    def force_stop():
                        if process.poll() is None:
                            process.kill()
                    timer = threading.Timer(3, force_stop)
                    timer.daemon = True
                    timer.start()
                return
            folder = self.cache/key
            if folder.exists():
                shutil.rmtree(folder)


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def local_request(self):
        host = self.headers.get('Host', '')
        expected = {f'localhost:{self.server.server_port}', f'127.0.0.1:{self.server.server_port}'}
        origin = self.headers.get('Origin')
        return host in expected and self.headers.get('Sec-Fetch-Site') != 'cross-site' and (not origin or origin == 'http://'+host)

    def reply(self, value, code=200):
        payload = json.dumps(value).encode()
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        route = urlparse(self.path).path
        if not route.startswith('/api/'):
            return super().do_GET()
        if not self.local_request():
            return self.reply({'error': 'Local same-origin requests only'}, 403)
        if route == '/api/stems':
            return self.reply({'available': self.server.jobs.available, 'active': self.server.jobs.active})
        m = re.fullmatch(r'/api/stems/([a-f0-9]{64})(?:/(drums|bass|vocals|other)\.wav)?', route)
        if not m:
            return self.reply({'error': 'Unknown analysis'}, 404)
        key, stem = m.groups()
        if stem:
            if self.server.jobs.status(key)['state'] != 'ready':
                return self.reply({'error': 'Stem audio is not cached; analyze this track again'}, 404)
            file = self.server.jobs.cache/key/(stem+'.wav')
            if not file.exists():
                return self.reply({'error': 'Stem audio is not cached'}, 404)
            os.utime(file.parent, None)
            total = file.stat().st_size
            start, end, code = 0, total-1, 200
            requested = self.headers.get('Range')
            if requested:
                match = re.fullmatch(r'bytes=(\d+)-(\d*)', requested)
                if not match or int(match[1]) >= total:
                    return self.reply({'error': 'Unsupported audio range'}, 416)
                start, end = int(match[1]), min(total-1, int(match[2]) if match[2] else total-1)
                if end < start:
                    return self.reply({'error': 'Invalid audio range'}, 416)
                code = 206
            self.send_response(code)
            self.send_header('Content-Type', 'audio/wav')
            self.send_header('Accept-Ranges', 'bytes')
            self.send_header('Content-Length', str(end-start+1))
            if code == 206:
                self.send_header('Content-Range', f'bytes {start}-{end}/{total}')
            self.end_headers()
            with file.open('rb') as f:
                f.seek(start)
                remaining = end-start+1
                while remaining:
                    block = f.read(min(65536, remaining))
                    if not block:
                        break
                    self.wfile.write(block)
                    remaining -= len(block)
            return
        with self.server.jobs.lock:
            return self.reply(self.server.jobs.status(key))

    def do_POST(self):
        if not self.local_request():
            return self.reply({'error': 'Local same-origin requests only'}, 403)
        if self.path != '/api/stems':
            return self.reply({'error': 'Unknown action'}, 404)
        source = self.server.jobs.cache/(uuid.uuid4().hex+'.upload')
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 44 < length <= MAX_UPLOAD:
                return self.reply({'error': 'Recording must be under 128 MB'}, 413)
            self.connection.settimeout(60)
            with source.open('wb') as f:
                remaining = length
                while remaining:
                    block = self.rfile.read(min(65536, remaining))
                    if not block:
                        raise ValueError('Upload interrupted')
                    f.write(block)
                    remaining -= len(block)
            result = self.server.jobs.submit(source)
            return self.reply(result, 202)
        except (ValueError, OSError, wave.Error, EOFError) as e:
            return self.reply({'error': str(e)}, 400)
        finally:
            source.unlink(missing_ok=True)

    def do_DELETE(self):
        if not self.local_request():
            return self.reply({'error': 'Local same-origin requests only'}, 403)
        m = re.fullmatch(r'/api/stems/([a-f0-9]{64})', self.path)
        if not m:
            return self.reply({'error': 'Unknown analysis'}, 404)
        self.server.jobs.cancel(m[1])
        self.reply({'ok': True})


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', type=int, default=8765)
    ap.add_argument('--cache', type=Path, default=Path.home()/'.cache/companion-music-lab/stems')
    ap.add_argument('--worker-python', default=os.environ.get('MUSIC_LAB_PYTHON', str(Path.home()/'.cache/companion-music-lab/venv/bin/python')))
    args = ap.parse_args()
    httpd = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    httpd.jobs = Jobs(args.cache, args.worker_python)
    print(f'Music Lab: http://127.0.0.1:{args.port} · local stem analysis {"ready" if httpd.jobs.available else "not installed (setup-ml.sh)"}', flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        if httpd.jobs.active:
            httpd.jobs.cancel(httpd.jobs.active)
    finally:
        httpd.server_close()


if __name__ == '__main__':
    main()
