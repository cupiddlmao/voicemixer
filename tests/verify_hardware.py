#!/usr/bin/env python3
"""Real five-strip DSP, selected-channel isolation and virtual unplug/replug."""
import array
import json
import os
from pathlib import Path
import selectors
import subprocess
import tempfile
import time
import verify_matrix as m


def samples(target, allow_suspended=False):
    p=m.spawn(['pw-record','--raw','--target',target,'--properties','node.dont-fallback=true state.restore-props=false state.default-volume=1.0','--rate','48000','--channels','1','--format','f32','--latency','256','-'],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL)
    sel=selectors.DefaultSelector();sel.register(p.stdout,selectors.EVENT_READ);data=bytearray();end=time.monotonic()+4
    try:
        while len(data)<96000 and time.monotonic()<end:
            if sel.select(.1):
                block=os.read(p.stdout.fileno(),96000-len(data))
                if not block:break
                data.extend(block)
        if allow_suspended and not data:return [0.0]*9600
        assert len(data)==96000,('no audio',target,len(data))
        values=array.array('f');values.frombytes(data);return values[-9600:]
    finally:
        p.terminate();p.wait(timeout=2);sel.close()


def feed(index):
    return m.spawn(['pw-loopback','--channels','2','--channel-map','[ FL FR ]','--name',f'{m.PREFIX}.feed{index}',
        '--capture-props',f'media.class=Audio/Sink node.name={m.PREFIX}.feed{index} node.virtual=true priority.session=0',
        '--playback-props',f'media.class=Audio/Source node.name={m.PREFIX}.test.feed{index} node.autoconnect=false node.virtual=true priority.session=0'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)


def main():
    with tempfile.TemporaryDirectory(prefix='mixer-hardware-') as directory:
        env=dict(os.environ,QT_QPA_PLATFORM='offscreen',LINUX_MIXER_STATE_DIR=directory)
        config=Path(directory)/'hardware.conf'
        subprocess.run([str(m.ROOT/'build/linux-mixer'),'--write-hardware',str(config)],env=env,check=True)
        data=json.loads(config.read_text())
        # Select left on strips 1/3/5 and right on 2/4 from stereo interfaces.
        filters=[v for v in data['context.modules'] if v['name']=='libpipewire-module-filter-chain']
        for i,module in enumerate(filters):
            props=module['args']['capture.props'];props['audio.position']=['MONO'];props['stream.dont-remix']=True
        config.write_text(json.dumps(data).replace('linux_mixer',m.PREFIX))
        settings=json.loads((Path(directory)/'settings.json').read_text())
        for i,hw in enumerate(settings['hardware']):hw['mic']=f'{m.PREFIX}.test.feed{i}';hw['channel']='FL' if i%2==0 else 'FR'
        (Path(directory)/'settings.json').write_text(json.dumps(settings))
        def reconnect():
            subprocess.run([str(m.ROOT/'build/linux-mixer'),'--link-hardware','--prefix',m.PREFIX],env=env,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)

        feeds=[feed(i) for i in range(5)]
        for i in range(5):m.wait_for(lambda i=i:m.node(f'{m.PREFIX}.test.feed{i}'),'feed source')
        with (Path(directory)/'hardware.log').open('w+') as log:
            m.spawn(['pipewire','-c',str(config)],stdout=log,stderr=log)
            try:
                for i in range(5):
                    name=f'{m.PREFIX}.processed' if i==0 else f'{m.PREFIX}.hardware{i+1}.processed'
                    m.wait_for(lambda:m.node(name),'processed hardware')
                    m.play(f'{m.PREFIX}.feed{i}',(333,777))
                reconnect();time.sleep(.3)
                for i in range(5):
                    name=f'{m.PREFIX}.processed' if i==0 else f'{m.PREFIX}.hardware{i+1}.processed'
                    values=samples(name);wanted=333 if i%2==0 else 777;unwanted=777 if i%2==0 else 333
                    assert abs(m.amplitude(values,wanted)-.03)<.002,(i,m.amplitude(values,wanted))
                    assert m.amplitude(values,unwanted)<.0015,('interface channel leaked',i)
                # A live per-strip fader and mute must leave the other microphone unchanged.
                capture=f'{m.PREFIX}.hardware2.capture';processed=f'{m.PREFIX}.hardware2.processed'
                m.controls(capture,{'fader:Mult':.25});assert abs(m.amplitude(samples(processed),777)-.0075)<.002
                m.controls(capture,{'fader:Mult':0});assert max(abs(v) for v in samples(processed))<1e-6
                assert abs(m.amplitude(samples(f'{m.PREFIX}.processed'),333)-.03)<.002
                m.controls(capture,{'fader:Mult':1})
                # Synthetic selected source disappears; the capture must never fall back to a real mic.
                feeds[1].terminate();feeds[1].wait(timeout=2);time.sleep(.3)
                assert max(abs(v) for v in samples(processed,allow_suspended=True))<1e-6,'disconnected input fell back or retained audio'
                feeds[1]=feed(1);m.wait_for(lambda:m.node(f'{m.PREFIX}.test.feed1'),'replacement interface')
                m.play(f'{m.PREFIX}.feed1',(333,777));reconnect();time.sleep(.4)
                assert abs(m.amplitude(samples(processed),777)-.03)<.002,'selected input did not reconnect'
                print('PASS: all 5 production mic chains, independent fader/mute, FL/FR interface selection, silence on disconnect and automatic selected-source reconnection. No speakers used.')
            except BaseException:
                log.seek(0);print(log.read());raise


if __name__=='__main__':
    try:main()
    finally:
        m.stop.set()
        for p in reversed(m.processes):
            if p.poll() is None:p.terminate()
        for p in m.processes:
            try:p.wait(timeout=2)
            except subprocess.TimeoutExpired:p.kill()
