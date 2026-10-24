#!/usr/bin/env python3
"""Check actual stereo position, independent strips and matching dry/FX routing."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import verify_matrix as m


def main():
    with tempfile.TemporaryDirectory(prefix='mixer-position-') as directory:
        folder=Path(directory);config=folder/'matrix.conf';settings=folder/'settings.json'
        pans=[-1,.5,1,0,-.5]
        routes=[[False]*8 for _ in range(8)]
        for i in range(5):routes[i][7]=True
        settings.write_text(json.dumps({'routing':{'hardwarePan':pans,'routes':routes,'reverbSend':[.25]*8,'delaySend':[.4]*8}}))
        subprocess.run([str(m.ROOT/'build/linux-mixer'),'--write-matrix',str(config),'--prefix',m.PREFIX,'--settings',str(settings)],env=dict(os.environ,QT_QPA_PLATFORM='offscreen'),check=True)
        data=json.loads(config.read_text())
        bus=next(v['args'] for v in data['context.modules'] if v.get('args',{}).get('capture.props',{}).get('node.name')==f'{m.PREFIX}.bus7.capture')
        nodes={n['name']:n for n in bus['filter.graph']['nodes']}
        for i,pan in enumerate(pans):
            l,r=1-max(0,pan),1+min(0,pan)
            for name,value in [('left',l),('right',r),('verb0',l*.25),('verb1',r*.25),('echo0',l*.4),('echo1',r*.4)]:
                assert abs(nodes[name]['control'][f'Gain {i+1}']-value)<1e-8,(i,name)
        with (folder/'graph.log').open('w+') as log:
            m.spawn(['pipewire','-c',str(config)],stdout=log,stderr=log)
            m.wait_for(lambda:m.node(f'{m.PREFIX}.b3'),'position graph')
            bus=f'{m.PREFIX}.bus7.capture'
            # A known mono tone stands in for a processed mic, never a physical device.
            for c in range(2):m.connect(f'{m.PREFIX}.input0.output','FL',bus,f'AUX{c}')
            m.controls(bus,{'fx:Reverb return':0,'fx:Delay return':0})
            m.play(f'{m.PREFIX}.system',(333,777))
            left,right=m.capture(7)
            assert abs(m.amplitude(left,333)-.03)<.002 and max(map(abs,right))<1e-6,'left position failed'
            m.controls(bus,{'left:Gain 1':0,'right:Gain 1':1})
            left,right=m.capture(7)
            assert max(map(abs,left))<1e-6 and abs(m.amplitude(right,333)-.03)<.002,'right position failed'
            m.controls(bus,{'left:Gain 1':1,'right:Gain 1':1})
            left,right=m.capture(7)
            assert abs(m.amplitude(left,333)-.03)<.002 and abs(m.amplitude(right,333)-.03)<.002,'center position failed'
            m.controls(bus,{'left:Gain 1':0,'right:Gain 1':0})
            for c in range(2):m.connect(f'{m.PREFIX}.input0.output','FL',bus,f'AUX{2+c}')
            left,right=m.capture(7)
            assert abs(m.amplitude(left,333)-.015)<.002 and abs(m.amplitude(right,333)-.03)<.002,'independent strip position failed'
            print('PASS: all five saved pan values reach dry/FX graph controls; actual mono audio pans left, right and center with independent strip gains. No physical devices used.')


if __name__=='__main__':
    try:main()
    finally:
        m.stop.set()
        for p in reversed(m.processes):
            if p.poll() is None:p.terminate()
        for p in reversed(m.processes):
            try:p.wait(timeout=2)
            except subprocess.TimeoutExpired:p.kill()
