#!/usr/bin/env python3
"""Hang a plugin host on a private PipeWire server; never touches desktop audio.
Requires Xvfb (or --xvfb /path/to/Xvfb). No internet access is used.
"""
import argparse
import json
import os
import re
from pathlib import Path
import select
import shutil
import signal
import subprocess
import tempfile
import threading
import time
import ctypes as C

ROOT = Path(__file__).resolve().parents[1]

def rack_window(display_name, click_hide=False, debug=False):
    """Inspect/click only the Xvfb display created by this test."""
    x=C.CDLL('libX11.so.6');ptr=C.c_void_p;window=C.c_ulong
    x.XOpenDisplay.argtypes=[C.c_char_p];x.XOpenDisplay.restype=ptr
    x.XDefaultRootWindow.argtypes=[ptr];x.XDefaultRootWindow.restype=window
    x.XQueryTree.argtypes=[ptr,window,C.POINTER(window),C.POINTER(window),C.POINTER(C.POINTER(window)),C.POINTER(C.c_uint)]
    x.XFetchName.argtypes=[ptr,window,C.POINTER(ptr)]
    x.XInternAtom.argtypes=[ptr,C.c_char_p,C.c_int];x.XInternAtom.restype=window
    x.XGetWindowProperty.argtypes=[ptr,window,window,C.c_long,C.c_long,C.c_int,window,C.POINTER(window),C.POINTER(C.c_int),C.POINTER(C.c_ulong),C.POINTER(C.c_ulong),C.POINTER(ptr)]
    x.XGetGeometry.argtypes=[ptr,window,C.POINTER(window),C.POINTER(C.c_int),C.POINTER(C.c_int),C.POINTER(C.c_uint),C.POINTER(C.c_uint),C.POINTER(C.c_uint),C.POINTER(C.c_uint)]
    x.XFree.argtypes=[ptr];x.XCloseDisplay.argtypes=[ptr]
    display=x.XOpenDisplay(display_name.encode());assert display
    try:
        root=x.XDefaultRootWindow(display);pending=[root];found=None;observed=[]
        while pending:
            current=pending.pop();name=ptr()
            title=''
            if x.XFetchName(display,current,C.byref(name)) and name:
                title=C.string_at(name).decode(errors='replace');x.XFree(name)
            actual=window();fmt=C.c_int();length=C.c_ulong();remaining=C.c_ulong();value=ptr()
            atom=x.XInternAtom(display,b'_NET_WM_NAME',0)
            if x.XGetWindowProperty(display,current,atom,0,1024,0,0,C.byref(actual),C.byref(fmt),C.byref(length),C.byref(remaining),C.byref(value))==0 and value:
                if fmt.value==8:title=C.string_at(value,length.value).decode(errors='replace')
                x.XFree(value)
            gx=C.c_int();gy=C.c_int();width=C.c_uint();height=C.c_uint();border=C.c_uint();depth=C.c_uint();geo_root=window()
            geometry=x.XGetGeometry(display,current,C.byref(geo_root),C.byref(gx),C.byref(gy),C.byref(width),C.byref(height),C.byref(border),C.byref(depth))
            # A bare Xvfb server may have no WM title property. The private
            # fixture rack is its only component with these client dimensions.
            observed.append((int(current),title,width.value,height.value))
            rack_size=geometry and current!=root and width.value==850 and height.value==190
            if ' - VST FX' in title or rack_size:found=current;break
            parent=window();returned_root=window();children=C.POINTER(window)();count=C.c_uint()
            if x.XQueryTree(display,current,C.byref(returned_root),C.byref(parent),C.byref(children),C.byref(count)):
                pending.extend(children[i] for i in range(count.value))
                if children:x.XFree(C.cast(children,ptr))
        if found and click_hide:
            xtst=C.CDLL('libXtst.so.6')
            xtst.XTestFakeMotionEvent.argtypes=[ptr,C.c_int,C.c_int,C.c_int,C.c_ulong]
            xtst.XTestFakeButtonEvent.argtypes=[ptr,C.c_uint,C.c_int,C.c_ulong]
            x.XTranslateCoordinates.argtypes=[ptr,window,window,C.c_int,C.c_int,C.POINTER(C.c_int),C.POINTER(C.c_int),C.POINTER(window)]
            x.XRaiseWindow.argtypes=[ptr,window];x.XSetInputFocus.argtypes=[ptr,window,C.c_int,C.c_ulong];x.XFlush.argtypes=[ptr]
            rx=C.c_int();ry=C.c_int();child=window()
            # Native-title-bar JUCE content is 850px wide; this point lies
            # inside its Hide rack button. No live desktop input is generated.
            assert x.XTranslateCoordinates(display,found,root,788,24,C.byref(rx),C.byref(ry),C.byref(child))
            x.XRaiseWindow(display,found);x.XSetInputFocus(display,found,2,0)
            xtst.XTestFakeMotionEvent(display,-1,rx.value,ry.value,0)
            x.XFlush(display);time.sleep(.05)
            xtst.XTestFakeButtonEvent(display,1,1,0);x.XFlush(display);time.sleep(.08)
            xtst.XTestFakeButtonEvent(display,1,0,0);x.XFlush(display)
        if debug:print("Private X11 windows:",observed)
        return found
    finally:x.XCloseDisplay(display)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--xvfb', default=shutil.which('Xvfb'))
    parser.add_argument('--screenshots', type=Path)
    parser.add_argument('--audio-tests', action='store_true')
    args = parser.parse_args()
    assert args.xvfb, 'Install Xvfb or pass --xvfb'
    with tempfile.TemporaryDirectory(prefix='voicemixer-stability-') as tmp:
        base = Path(tmp); runtime = base/'runtime'; runtime.mkdir(mode=0o700)
        state = base/'profile'; state.mkdir()
        log = (base/'test.log').open('w+')
        processes = []
        env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), PIPEWIRE_RUNTIME_DIR=str(runtime),
                   LINUX_MIXER_STATE_DIR=str(state), QT_QPA_PLATFORM='offscreen', PULSE_SERVER='unix:'+str(runtime/'no-pulse'))
        # A separate display is also needed by the JUCE plugin host.
        read_fd, write_fd = os.pipe()
        display = subprocess.Popen([args.xvfb, '-displayfd', str(write_fd), '-screen', '0', '1280x800x24', '-nolisten', 'tcp'], pass_fds=(write_fd,), stdout=log, stderr=log)
        processes.append(display); os.close(write_fd)
        try:
            assert select.select([read_fd], [], [], 5)[0], 'Xvfb did not start'
            env['DISPLAY'] = ':'+os.read(read_fd, 32).decode().strip(); os.close(read_fd)
            daemon = subprocess.Popen(['pipewire'], env=env, stdout=log, stderr=log); processes.append(daemon)
            def wait(predicate, label, seconds=12):
                end = time.monotonic()+seconds
                while time.monotonic()<end:
                    value = predicate()
                    if value:return value
                    time.sleep(.15)
                log.flush();print((base/'test.log').read_text()[-3000:])
                for f in (state/'.runtime').glob('*.log'):print(f.name, f.read_text(errors='replace')[-3000:])
                print('Nodes:',list(nodes()))
                rack_window(env['DISPLAY'],debug=True)
                if args.screenshots:(args.screenshots/'failure-graph.json').write_text(json.dumps(graph(),indent=2))
                raise AssertionError('Timed out: '+label)
            def graph():
                p = subprocess.run(['pw-dump'], env=env, capture_output=True, timeout=3)
                return json.loads(p.stdout) if p.returncode==0 else []
            def nodes():
                return {n.get('info',{}).get('props',{}).get('node.name'): n for n in graph() if n.get('type')=='PipeWire:Interface:Node'}
            wait(lambda:(runtime/'pipewire-0').exists(), 'private PipeWire socket')
            if args.screenshots:env['LINUX_MIXER_UI_TEST_OUTPUT'] = str(args.screenshots.resolve())
            subprocess.run([str(ROOT/'build/linux-mixer'), '--health-ui-test'], env=env, check=True, timeout=15, stdout=log, stderr=log)
            fixture=ROOT/'build/tests/MixerTestGain.vst3'
            assert fixture.is_dir(), 'Build the mixer_test_vst3 target first'
            descriptions=json.loads(subprocess.check_output([str(ROOT/'build/linux-mixer-plugin-host'),'--probe',str(fixture)],env=env,stderr=log,text=True,timeout=10))
            rack=state/'plugin-racks/input5';rack.mkdir(parents=True)
            (rack/'rack.json').write_text(json.dumps({'plugins':[{'description':descriptions[0],'state':'','bypass':False}]}))
            (state/'settings.json').write_text(json.dumps({'version':3, 'engineEnabled':True, 'mic':'', 'pluginRacks':[False]*5+[True,False,False]}))
            controller = subprocess.Popen([str(ROOT/'build/linux-mixer')], env=env, stdout=log, stderr=log); processes.append(controller)
            host_name = 'linux_mixer.plugins.input5'
            wait(lambda:host_name in nodes(), 'private input plugin rack')
            wait(lambda:'linux_mixer.bus0.capture' in nodes(), 'private routing matrix')
            # This private server has no session manager. Configure only our
            # test adapters' DSP ports, which WirePlumber normally configures.
            for n in graph():
                props=n.get('info',{}).get('props',{})
                kind=props.get('media.class')
                if n.get('type')!='PipeWire:Interface:Node' or 'adapt.follower.spa-node' not in props:continue
                if n.get('info',{}).get('n-input-ports',0) or n.get('info',{}).get('n-output-ports',0):continue
                positions=props.get('audio.position',[])
                if isinstance(positions,str):positions=re.findall(r'[A-Z]+[0-9]*',positions)
                if not positions:continue
                config={'direction':'Input' if n['info'].get('max-input-ports',0)>0 else 'Output','mode':'dsp','format':{'mediaType':'audio','mediaSubtype':'raw','format':'F32P','rate':48000,'channels':len(positions),'position':positions}}
                subprocess.run(['pw-cli','set-param',str(n['id']),'PortConfig',json.dumps(config)],env=env,stdout=log,stderr=log,check=True,timeout=3)
            heartbeat = state/'plugin-racks/input5/heartbeat.json'
            def heartbeat_data():
                try:return json.loads(heartbeat.read_text())
                except (FileNotFoundError, json.JSONDecodeError):return None
            def route_from(source_name):
                data=graph()
                names={n.get('info',{}).get('props',{}).get('node.name'):n['id'] for n in data if n.get('type')=='PipeWire:Interface:Node'}
                ports={n['id']:n.get('info',{}).get('props',{}).get('node.id') for n in data if n.get('type')=='PipeWire:Interface:Port'}
                source=names.get(source_name);dest=names.get('linux_mixer.bus0.capture')
                return source is not None and dest is not None and sum(ports.get(n.get('info',{}).get('output-port-id'))==source and ports.get(n.get('info',{}).get('input-port-id'))==dest for n in data if n.get('type')=='PipeWire:Interface:Link')>=2
            wait(lambda:route_from(host_name), 'plugin inserted on both channels')
            wait(lambda:(rack/'visibility.json').exists(), 'background visibility state')
            assert json.loads((rack/'visibility.json').read_text()).get('visible') is False
            assert rack_window(env['DISPLAY']) is None, 'background startup created a native rack window'
            (rack/'hide.request').write_text('hide')
            wait(lambda:not (rack/'hide.request').exists(), 'hide rack without stopping DSP')
            wait(lambda:json.loads((rack/'visibility.json').read_text()).get('visible') is False, 'hidden window state')
            assert host_name in nodes(), 'hiding stopped the plugin host'
            (rack/'show.request').write_text('show')
            wait(lambda:not (rack/'show.request').exists(), 'reopen hidden rack')
            wait(lambda:json.loads((rack/'visibility.json').read_text()).get('visible') is True, 'visible window state')
            first_window=wait(lambda:rack_window(env['DISPLAY']), 'rack native window created on request')
            rack_window(env['DISPLAY'],click_hide=True)
            wait(lambda:json.loads((rack/'visibility.json').read_text()).get('visible') is False, 'real Hide rack mouse click')
            assert host_name in nodes(), 'mouse hide stopped audio processing'
            (rack/'show.request').write_text('show')
            wait(lambda:json.loads((rack/'visibility.json').read_text()).get('visible') is True, 'reopen after mouse hide')
            assert rack_window(env['DISPLAY'])==first_window, 'reopen replaced the existing rack window'
            info = wait(heartbeat_data, 'plugin heartbeat')
            pid = info['pid']; assert pid>1
            # Verify the PID belongs to this temporary test before signalling it.
            command = Path(f'/proc/{pid}/cmdline').read_bytes()
            assert str(state/'plugin-racks/input5').encode() in command
            wait(lambda:heartbeat_data().get('sequence',0)>info['sequence'], 'heartbeat progress')
            os.kill(pid, signal.SIGSTOP)
            wait(lambda:host_name not in nodes(), 'watchdog removed frozen rack', seconds=24)
            wait(lambda:route_from('linux_mixer.input0.output'),'normal input routing after hang')
            assert controller.poll() is None, 'controller crashed'
            time.sleep(2);assert host_name not in nodes(),'failed plugin restarted in a crash loop'
            (state/'.runtime/quit.request').write_text('stop');controller.wait(timeout=10)
            assert controller.returncode==0
            if args.audio_tests:
                # Configure only test adapters while the stereo audio integration
                # test runs on this server without a session manager.
                done=threading.Event();configured=set()
                def configure():
                    while not done.is_set():
                        for n in graph():
                            props=n.get('info',{}).get('props',{});serial=props.get('object.serial')
                            if n.get('type')!='PipeWire:Interface:Node' or 'adapt.follower.spa-node' not in props or serial in configured:continue
                            positions=props.get('audio.position',[]) or next(iter(n.get('info',{}).get('params',{}).get('EnumFormat',[])),{}).get('position',[])
                            if isinstance(positions,str):positions=re.findall(r'[A-Z]+[0-9]*',positions)
                            if not positions:continue
                            direction='Input' if n['info'].get('max-input-ports',0)>0 else 'Output'
                            config={'direction':direction,'mode':'dsp','format':{'mediaType':'audio','mediaSubtype':'raw','format':'F32P','rate':48000,'channels':len(positions),'position':positions}}
                            subprocess.run(['pw-cli','set-param',str(n['id']),'PortConfig',json.dumps(config)],env=env,stdout=log,stderr=log,timeout=3)
                            configured.add(serial)
                        done.wait(.1)
                thread=threading.Thread(target=configure,daemon=True);thread.start()
                try:
                    subprocess.run(['python',str(ROOT/'tests/verify_plugins.py')],env=dict(env,LINUX_MIXER_NO_SESSION_MANAGER='1'),stdout=log,stderr=log,check=True,timeout=60)
                except subprocess.CalledProcessError:
                    log.flush();print((base/'test.log').read_text()[-6000:]);raise
                finally:done.set();thread.join(timeout=5)
                print('PASS: stereo VST3 audio/chain/state tests with silent auxiliary inputs on the private server.')
            print('PASS: private-server UI checks, plugin heartbeat, frozen-host detection, normal routing recovery, no automatic crash loop, clean shutdown. Desktop audio was untouched.')
        finally:
            for kind in ('engine','matrix'):
                try:
                    pid=int((state/f'.runtime/{kind}.pid').read_text())
                    if str(state).encode() in Path(f'/proc/{pid}/cmdline').read_bytes():os.kill(pid,signal.SIGTERM)
                except (FileNotFoundError,ProcessLookupError):pass
            for p in reversed(processes):
                if p.poll() is None:p.terminate()
            for p in reversed(processes):
                try:p.wait(timeout=3)
                except subprocess.TimeoutExpired:p.kill();p.wait()
            log.close()
if __name__=='__main__':main()
