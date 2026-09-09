import hashlib
import http.client
import io
import json
from pathlib import Path
import tempfile
import threading
import time
import unittest
import wave
from server import ThreadingHTTPServer,Handler,Jobs


class ServerTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.cache=Path(self.temp.name)
        self.jobs=Jobs(self.cache,'/no-python');self.jobs.available=True
        self.server=ThreadingHTTPServer(('127.0.0.1',0),Handler);self.server.jobs=self.jobs
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start()
        b=io.BytesIO()
        with wave.open(b,'wb') as w:w.setparams((2,2,16000,0,'NONE','not compressed'));w.writeframes(bytes(1024))
        self.wav=b.getvalue();self.key=hashlib.sha256(self.wav).hexdigest()
        p=self.cache/self.key;p.mkdir();(p/'reference.json').write_text(json.dumps({'test':True}));(p/'drums.wav').write_bytes(self.wav)
    def tearDown(self):
        self.server.shutdown();self.server.server_close();self.temp.cleanup()
    def call(self,method,path,body=None,headers=None):
        c=http.client.HTTPConnection('127.0.0.1',self.server.server_port)
        c.request(method,path,body,headers or {});r=c.getresponse();data=r.read();out=(r.status,dict(r.getheaders()),data);c.close();return out
    def test_ready_reuse_and_range(self):
        status,_,body=self.call('POST','/api/stems',self.wav,{'Content-Type':'audio/wav'})
        self.assertEqual(status,202);self.assertEqual(json.loads(body)['id'],self.key);self.assertIsNone(self.jobs.active)
        status,headers,body=self.call('GET',f'/api/stems/{self.key}/drums.wav',headers={'Range':'bytes=44-59'})
        self.assertEqual(status,206);self.assertEqual(body,self.wav[44:60]);self.assertEqual(headers['Content-Range'],f'bytes 44-59/{len(self.wav)}')
        self.assertEqual(self.call('GET',f'/api/stems/{self.key}/drums.wav',headers={'Range':'bytes=999999-'})[0],416)
        self.assertEqual(self.call('DELETE',f'/api/stems/{self.key}')[0],200)
        self.assertEqual(json.loads(self.call('GET',f'/api/stems/{self.key}')[2])['state'],'missing')
    def test_origin_and_invalid_input(self):
        self.assertEqual(self.call('GET','/api/stems',headers={'Origin':'https://unrelated.example'})[0],403)
        self.assertEqual(self.call('GET','/api/stems',headers={'Host':'unrelated.example'})[0],403)
        self.assertEqual(self.call('POST','/api/stems',b'x'*100)[0],400)
        self.assertEqual(self.call('GET','/api/stems/../source.wav')[0],404)
        self.assertEqual(list(self.cache.glob('*.upload')),[])
    def test_worker_failure_and_recovery(self):
        self.jobs.python='/usr/bin/false'
        raw=self.wav[:-1]+b'1';key=hashlib.sha256(raw).hexdigest()
        self.assertEqual(self.call('POST','/api/stems',raw)[0],202)
        until=time.monotonic()+3
        while self.jobs.active and time.monotonic()<until:time.sleep(.01)
        self.assertIsNone(self.jobs.active)
        self.assertEqual(self.jobs.status(key)['state'],'failed')
        self.assertFalse((self.cache/key/'source.wav').exists())

    def test_cancel_state(self):
        other='b'*64;(self.cache/other).mkdir();self.jobs.active=other
        self.assertEqual(self.call('DELETE',f'/api/stems/{other}')[0],200)
        self.assertIn(other,self.jobs.cancelled)


if __name__=='__main__':unittest.main()
