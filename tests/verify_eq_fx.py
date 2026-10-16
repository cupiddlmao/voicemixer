#!/usr/bin/env python3
"""Production filter graph: stereo EQ, balance/mono, echo and reverb sends/returns."""
import array
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import verify_matrix as m


def main():
    with tempfile.TemporaryDirectory(prefix='mixer-eq-fx-') as folder:
        config=Path(folder)/'matrix.conf'
        subprocess.run([str(m.ROOT/'build/linux-mixer'),'--write-matrix',str(config),'--prefix',m.PREFIX],env=dict(os.environ,QT_QPA_PLATFORM='offscreen'),check=True)
        with (Path(folder)/'graph.log').open('w+') as log:
            m.spawn(['pipewire','-c',str(config)],stdout=log,stderr=log)
            m.wait_for(lambda:m.node(f'{m.PREFIX}.b3'),'EQ/FX graph')
            for c,channel in enumerate(('FL','FR')):m.connect(f'{m.PREFIX}.input0.output',channel,f'{m.PREFIX}.bus7.capture',f'AUX{10+c}')
            m.controls(f'{m.PREFIX}.bus7.capture',{'left:Gain 6':1,'right:Gain 6':1})
            m.play(f'{m.PREFIX}.system',(10000,12000));tone=m.processes[-1]
            left,right=m.capture(7);assert abs(m.amplitude(left,10000)-.03)<.002 and abs(m.amplitude(right,12000)-.03)<.002
            m.controls(f'{m.PREFIX}.system',{'leftEq0:Gain':6,'rightEq0:Gain':-6})
            left,right=m.capture(7);assert m.amplitude(left,10000)>.045 and m.amplitude(right,12000)<.022,'independent stereo EQ failed'
            m.controls(f'{m.PREFIX}.bus7.capture',{'monoL:Gain 1':.5,'monoL:Gain 2':.5,'monoR:Gain 1':.5,'monoR:Gain 2':.5})
            left,right=m.capture(7)
            for f in (10000,12000):assert abs(m.amplitude(left,f)-m.amplitude(right,f))<.001,'mono bus mismatch'
            m.controls(f'{m.PREFIX}.bus7.capture',{'gainL:Mult':0});left,right=m.capture(7);assert max(map(abs,left))<1e-6 and max(map(abs,right))>.01,'balance failed'
            tone.terminate();tone.wait(timeout=2)
            m.controls(f'{m.PREFIX}.system',{'leftEq0:Gain':0,'rightEq0:Gain':0})
            m.controls(f'{m.PREFIX}.bus7.capture',{'gainL:Mult':1,'monoL:Gain 1':1,'monoL:Gain 2':0,'monoR:Gain 1':0,'monoR:Gain 2':1,'echo0:Gain 6':1,'fx:Delay ms':50,'fx:Feedback':0,'fx:Delay return':1})
            player=m.spawn(['pw-play','--raw','--target',f'{m.PREFIX}.system','--properties','node.dont-fallback=true state.restore-props=false state.restore-target=false state.default-volume=1.0','--rate','48000','--channels','2','--format','f32','--latency','256','-'],stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            signal=array.array('f',(.2 if frame%4800==0 and channel==0 else 0 for frame in range(48000) for channel in range(2))).tobytes()
            def feed():
                try:
                    while not m.stop.is_set():player.stdin.write(signal);player.stdin.flush()
                except (BrokenPipeError,OSError):pass
                finally:
                    try:player.stdin.close()
                    except (BrokenPipeError,OSError):pass
            threading.Thread(target=feed,daemon=True).start();time.sleep(.25)
            left,right=m.capture(7);peaks=[i for i,v in enumerate(left) if v>.1]
            assert len(peaks)>=3 and all(abs(b-a-2400)<=1 for a,b in zip(peaks,peaks[1:])),('echo timing/send failed',peaks)
            assert max(map(abs,right))<1e-6,'echo crossed stereo channels'
            m.controls(f'{m.PREFIX}.bus7.capture',{'fx:Delay return':0});left,right=m.capture(7);peaks=[i for i,v in enumerate(left) if v>.1]
            assert len(peaks)>=1 and all(abs(b-a-4800)<=1 for a,b in zip(peaks,peaks[1:])),'muted delay return still audible'
            m.controls(f'{m.PREFIX}.bus7.capture',{'verb0:Gain 6':1,'fx:Reverb return':1})
            time.sleep(.2);left,right=m.capture(7);tail=sum(v*v for v in left if abs(v)<.1)
            assert tail>.0001 and max(map(abs,right))<1e-6,'reverb tail/return failed'
            print('PASS: independent stereo EQ, mono downmix, balance, 50 ms echo, send/return isolation, reverb tail and stereo separation in the production graph. No speakers used.')

if __name__=='__main__':
    try:main()
    finally:
        m.stop.set()
        for p in reversed(m.processes):
            if p.poll() is None:p.terminate()
        for p in m.processes:
            try:p.wait(timeout=2)
            except subprocess.TimeoutExpired:p.kill()
