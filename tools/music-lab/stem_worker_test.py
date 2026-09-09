import hashlib
import json
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch
import wave
import numpy as np
import soundfile as sf
from stem_worker import analyze, core_windows, db_curve, STEMS


class WorkerTest(unittest.TestCase):
    def test_context_and_levels(self):
        windows=list(core_windows(16000*125+7))
        self.assertEqual(windows[0],(0,960000,0,992000))
        self.assertEqual(windows[-1][1],16000*125+7)
        self.assertEqual(sum(b-a for a,b,_,_ in windows),16000*125+7)
        self.assertEqual(db_curve(np.zeros((1,2))),[-1200])
        self.assertEqual(db_curve(np.full((1600,2),.1)),[-200])

    def test_multichunk_clock_and_original(self):
        class FakeSeparator:
            def __init__(self,output_dir,**kwargs):self.output=Path(output_dir)
            def load_model(self,**kwargs):pass
            def separate(self,path):
                x,rate=sf.read(path);names=[]
                for i,stem in enumerate(STEMS):
                    name=f'passage_({stem})_htdemucs.wav';sf.write(self.output/name,x/(2**i),rate,subtype='PCM_16');names.append(name)
                return names
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);source=root/'source.wav';out=root/'result';out.mkdir()
            frames=16000*61+17
            pcm=np.column_stack((np.arange(frames)%2048,1024-np.arange(frames)%2048)).astype('<i2')
            with wave.open(str(source),'wb') as w:w.setparams((2,2,16000,0,'NONE','not compressed'));w.writeframes(pcm.tobytes())
            original=source.read_bytes()
            modules={'torch':types.SimpleNamespace(set_num_threads=lambda n:None,__version__='test',Tensor=type('Tensor',(),{})),
                     'audio_separator.separator':types.SimpleNamespace(Separator=FakeSeparator)}
            with patch.dict('sys.modules',modules):result=analyze(source,out,root/'models')
            self.assertEqual(result['source']['wavSHA256'],hashlib.sha256(original).hexdigest())
            self.assertEqual(source.read_bytes(),original)
            self.assertEqual(len(result['levels']['Drums']),(frames+1599)//1600)
            with wave.open(str(out/'drums.wav'),'rb') as w:
                self.assertEqual(w.getnframes(),frames)
                self.assertEqual(w.readframes(frames),pcm.tobytes())
            self.assertEqual(json.loads((out/'progress.json').read_text())['progress'],1)
            self.assertLess((out/'reference.json').stat().st_size,25000)


if __name__=='__main__':unittest.main()
