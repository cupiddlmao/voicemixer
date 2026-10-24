#!/usr/bin/env python3
"""Real VST3 discovery, stereo audio, chain order/bypass and saved state.
Uses an isolated synthetic matrix; never connects to a physical output.
Requires a desktop DISPLAY (the plugin host uses native plugin editors).
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import verify_matrix as m


def main():
    fixture=m.ROOT/'build/tests/MixerTestGain.vst3'
    host=m.ROOT/'build/linux-mixer-plugin-host'
    descriptions=json.loads(subprocess.check_output([str(host),'--probe',str(fixture)],text=True))
    assert len(descriptions)==1,'VST3 fixture was not discovered'
    with tempfile.TemporaryDirectory(prefix='mixer-vst-') as temp:
        folder=Path(temp);rack=folder/'rack';rack.mkdir();config=folder/'matrix.conf'
        subprocess.run([str(m.ROOT/'build/linux-mixer'),'--write-matrix',str(config),'--prefix',m.PREFIX],env=dict(os.environ,QT_QPA_PLATFORM='offscreen'),check=True)
        log=(folder/'graph.log').open('w+')
        m.spawn(['pipewire','-c',str(config)],stdout=log,stderr=log)
        m.wait_for(lambda:m.node(f'{m.PREFIX}.b3'),'isolated matrix')
        bus=f'{m.PREFIX}.bus7.capture'
        values={f'{side}:Gain {i}':0 for side in ('left','right') for i in range(1,9)}
        values.update({'left:Gain 1':1,'right:Gain 1':1,'fx:Reverb return':0,'fx:Delay return':0})
        m.controls(bus,values)
        m.play(f'{m.PREFIX}.system',(333,777))
        def check(chain,amplitude):
            (rack/'rack.json').write_text(json.dumps({'plugins':[{'description':descriptions[0],'state':state,'bypass':bypass} for state,bypass in chain]}))
            name=f'{m.PREFIX}.plugin';m.port_cache.clear()
            p=m.spawn([str(host),str(rack),name,'Plugin integration test','--background'],stdout=log,stderr=log)
            m.wait_for(lambda:m.node(name),'VST3 host node')
            for c in ('FL','FR'):
                m.connect(f'{m.PREFIX}.input0.output',c,name,c)
                m.connect(name,c,bus,'AUX0' if c=='FL' else 'AUX1')
            time.sleep(.3)
            left,right=m.capture(7)
            assert abs(m.amplitude(left,333)-amplitude)<.002,(amplitude,m.amplitude(left,333))
            assert abs(m.amplitude(right,777)-amplitude)<.002,'stereo right channel was lost'
            assert m.amplitude(left,777)<.001 and m.amplitude(right,333)<.001,'channel crosstalk'
            (rack/'quit.request').write_text('quit');p.wait(timeout=5);assert p.returncode==0
            saved=json.loads((rack/'rack.json').read_text())
            assert len(saved['plugins'])==len(chain) and all(s['state'] for s in saved['plugins']),'plugin state was not saved'
            m.wait_for(lambda:not m.node(name),'host shutdown')
            return saved
        saved=check([('',False)],.015)
        check([(saved['plugins'][0]['state'],False)],.015)
        check([('',False),('',False)],.0075)
        check([('',False),('',True)],.015)
        check([], .03)
        print('PASS: actual VST3 discovery and stereo DSP, two-effect chain, bypass, empty-rack passthrough, saved plugin state and clean worker shutdown. No physical devices used.')


if __name__=='__main__':
    try:main()
    finally:
        m.stop.set()
        for p in reversed(m.processes):
            if p.poll() is None:p.terminate()
        for p in reversed(m.processes):
            try:p.wait(timeout=2)
            except subprocess.TimeoutExpired:p.kill()
