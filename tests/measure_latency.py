#!/usr/bin/env python3
"""Measure relative software matrix delay with a synchronous reference channel.
This does not include a microphone ADC, headphone DAC, RNNoise, or a voice app.
"""
import array
import json
import os
from pathlib import Path
import selectors
import subprocess
import tempfile
import time
import verify_matrix as m


def main():
    with tempfile.TemporaryDirectory(prefix='mixer-latency-') as directory:
        config=Path(directory)/'matrix.conf'
        subprocess.run([str(m.ROOT/'build/linux-mixer'),'--write-matrix',str(config),'--prefix',m.PREFIX],env=dict(os.environ,QT_QPA_PLATFORM='offscreen'),check=True)
        with (Path(directory)/'graph.log').open('w') as log:
            m.spawn(['pipewire','-c',str(config)],stdout=log,stderr=log)
            m.wait_for(lambda:m.node(f'{m.PREFIX}.microphone'),'matrix')
            m.spawn(['pw-loopback','--channels','1','--channel-map','[ MONO ]','--name',m.PREFIX,
                '--capture-props',f'media.class=Audio/Sink node.name={m.PREFIX}.feed node.virtual=true priority.session=0',
                '--playback-props',f'media.class=Audio/Source node.name={m.PREFIX}.processed node.autoconnect=false node.virtual=true priority.session=0'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            m.wait_for(lambda:m.node(f'{m.PREFIX}.processed'),'reference source')
            for channel in range(2):m.connect(f'{m.PREFIX}.processed','MONO',f'{m.PREFIX}.bus5.capture',f'AUX{channel}')
            recorder=m.spawn(['pw-record','--raw','--target','0','--properties',f'node.name={m.PREFIX}.reference node.autoconnect=false node.dont-fallback=true state.restore-props=false state.default-volume=1.0','--rate','48000','--channels','2','--format','f32','--latency','256','-'],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL)
            m.wait_for(lambda:m.node(f'{m.PREFIX}.reference'),'synchronous recorder')
            m.connect(f'{m.PREFIX}.processed','MONO',f'{m.PREFIX}.reference','FL')
            m.connect(f'{m.PREFIX}.microphone','FL',f'{m.PREFIX}.reference','FR')
            player=m.spawn(['pw-play','--raw','--target',f'{m.PREFIX}.feed','--properties','node.dont-fallback=true state.restore-props=false state.restore-target=false state.default-volume=1.0','--rate','48000','--channels','1','--format','f32','--latency','256','-'],stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            # Widely spaced impulses make the sample displacement directly observable.
            signal=array.array('f',(0.2 if i%4096==0 else 0.0 for i in range(48000*2)))
            import threading
            def write():
                try:player.stdin.write(signal.tobytes());player.stdin.close()
                except (BrokenPipeError,OSError):pass
            threading.Thread(target=write,daemon=True).start()
            sel=selectors.DefaultSelector();sel.register(recorder.stdout,selectors.EVENT_READ);data=bytearray();deadline=time.monotonic()+4
            while len(data)<48000*2*4 and time.monotonic()<deadline:
                if sel.select(.1):data.extend(os.read(recorder.stdout.fileno(),32768))
            sel.close();values=array.array('f');values.frombytes(data[:len(data)//4*4]);left=values[::2];right=values[1::2]
            l=[i for i,v in enumerate(left) if v>.1];r=[i for i,v in enumerate(right) if v>.1]
            assert len(l)>=5 and len(r)>=5,('not enough reference impulses',len(l),len(r))
            delays=[min((j-i for j in r if j>=i),default=99999) for i in l[1:-1]]
            # A quantum or less is expected; no arbitrary latency target changes the system config.
            delay=sorted(delays)[len(delays)//2];assert delay<4096,(delay,delays)
            result={'kind':'relative software matrix delay','rate':48000,'delay_samples':delay,'delay_ms':round(delay/48,3),'trials':len(delays),'min_samples':min(delays),'max_samples':max(delays),'excludes':['microphone ADC','headphone DAC','mic DSP/RNNoise','voice application']}
            print(json.dumps(result,indent=2));(m.ROOT/'.runtime/latency-result.json').write_text(json.dumps(result,indent=2)+'\n')

if __name__=='__main__':
    try:main()
    finally:
        m.stop.set()
        for p in reversed(m.processes):
            if p.poll() is None:p.terminate()
        for p in m.processes:
            try:p.wait(timeout=2)
            except subprocess.TimeoutExpired:p.kill()
