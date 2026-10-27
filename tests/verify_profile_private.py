#!/usr/bin/env python3
"""Complete profile export/restore with a real test VST3 on a private PipeWire/Xvfb server."""
import os,json,re,subprocess,tempfile,time,select,shutil,argparse
from pathlib import Path
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--xvfb',default=shutil.which('Xvfb'));args=parser.parse_args();assert args.xvfb,'Pass --xvfb /path/to/Xvfb'
with tempfile.TemporaryDirectory(prefix='voicemixer-profile-') as tmp:
    runtime=Path(tmp);runtime.chmod(0o700)
    env=dict(os.environ,XDG_RUNTIME_DIR=tmp,PIPEWIRE_RUNTIME_DIR=tmp,QT_QPA_PLATFORM='offscreen',PULSE_SERVER='unix:'+tmp+'/no-pulse',LINUX_MIXER_STATE_DIR=tmp+'/profile')
    with (runtime/'profile.log').open('w+') as log:
        read_fd,write_fd=os.pipe()
        display=subprocess.Popen([args.xvfb,'-displayfd',str(write_fd),'-screen','0','1280x800x24','-nolisten','tcp'],pass_fds=(write_fd,),stdout=log,stderr=log);os.close(write_fd)
        assert select.select([read_fd],[],[],5)[0],'private display failed'
        env['DISPLAY']=':'+os.read(read_fd,32).decode().strip();os.close(read_fd)
        daemon=subprocess.Popen(['pipewire'],env=env,stdout=log,stderr=log)
        test=None
        try:
            end=time.monotonic()+5
            while not (runtime/'pipewire-0').exists():
                assert time.monotonic()<end,'private PipeWire did not start'
                time.sleep(.05)
            fixture=root/'build/tests/MixerTestGain.vst3'
            descriptions=json.loads(subprocess.check_output([str(root/'build/linux-mixer-plugin-host'),'--probe',str(fixture)],env=env,stderr=log,timeout=10))
            profile=runtime/'profile';rack=profile/'plugin-racks/input5';rack.mkdir(parents=True)
            (rack/'rack.json').write_text(json.dumps({'plugins':[{'description':descriptions[0],'state':'','bypass':False}]}))
            (profile/'settings.json').write_text(json.dumps({'version':3,'engineEnabled':True,'mic':'','pluginRacks':[False]*5+[True,False,False]}))
            test=subprocess.Popen([str(root/'build/linux-mixer'),'--profile-flow-test'],env=env,stdout=log,stderr=log)
            end=time.monotonic()+45
            while test.poll() is None and time.monotonic()<end:
                graph=json.loads(subprocess.check_output(['pw-dump'],env=env,timeout=3))
                for n in graph:
                    info=n.get('info',{});props=info.get('props',{})
                    if n.get('type')!='PipeWire:Interface:Node':continue
                    if info.get('n-input-ports',0) or info.get('n-output-ports',0):continue
                    positions=props.get('audio.position',[])
                    if isinstance(positions,str):positions=re.findall(r'[A-Z]+[0-9]*',positions)
                    if not positions:
                        formats=info.get('params',{}).get('EnumFormat',[])
                        positions=next((f.get('position',[]) for f in formats if f.get('position')),[])
                    if not positions:continue
                    config={'direction':'Input' if info.get('max-input-ports',0)>0 else 'Output','mode':'dsp','monitor':True,'format':{'mediaType':'audio','mediaSubtype':'raw','format':'F32P','rate':48000,'channels':len(positions),'position':positions}}
                    subprocess.run(['pw-cli','set-param',str(n['id']),'PortConfig',json.dumps(config)],env=env,stdout=log,stderr=log,timeout=3,check=True)
                # Link only explicitly targeted streams on this private server.
                names={n.get('info',{}).get('props',{}).get('node.name'):n['id'] for n in graph if n.get('type')=='PipeWire:Interface:Node'}
                ports=[n for n in graph if n.get('type')=='PipeWire:Interface:Port']
                edges={(n.get('info',{}).get('output-port-id'),n.get('info',{}).get('input-port-id')) for n in graph if n.get('type')=='PipeWire:Interface:Link'}
                for n in graph:
                    props=n.get('info',{}).get('props',{});target=props.get('target.object');kind=props.get('media.class','')
                    if target not in names or not kind.startswith('Stream/'):continue
                    source,dest=(n['id'],names[target]) if kind=='Stream/Output/Audio' else (names[target],n['id'])
                    for out in ports:
                        oi=out.get('info',{});op=oi.get('props',{})
                        if op.get('node.id')!=source or oi.get('direction')!='output':continue
                        for inp in ports:
                            ii=inp.get('info',{});ip=ii.get('props',{})
                            if ip.get('node.id')==dest and ii.get('direction')=='input' and op.get('audio.channel')==ip.get('audio.channel') and (out['id'],inp['id']) not in edges:
                                subprocess.run(['pw-link',str(out['id']),str(inp['id'])],env=env,stdout=log,stderr=log,timeout=3)
                time.sleep(.1)
            assert test.poll() is not None,'profile flow test exceeded timeout'
            log.flush();print('\n'.join(line for line in (runtime/'profile.log').read_text().splitlines() if any(word in line for word in ['PASS:', 'FAIL:', 'amplitude', 'Composite ', 'Loop state:'])))
            if test.returncode:print((runtime/'profile.log').read_text()[-4000:])
            assert test.returncode==0,'profile flow test failed'
        finally:
            if test is not None and test.poll() is None:test.terminate();test.wait(timeout=5)
            daemon.terminate();daemon.wait(timeout=5)
            display.terminate();display.wait(timeout=5)
