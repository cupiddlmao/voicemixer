#!/usr/bin/env python3
"""Exercise the production matrix with isolated synthetic audio, never speakers."""
import array
import cmath
import json
import math
import os
from pathlib import Path
import selectors
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
PREFIX = f'mixer_verify_{os.getpid()}'
processes = []
stop = threading.Event()
port_cache = {}


def run(*args):
    return subprocess.check_output(args, stderr=subprocess.STDOUT, timeout=8)


def spawn(args, **kwargs):
    p = subprocess.Popen(args, **kwargs)
    processes.append(p)
    return p


def graph():
    return json.loads(run('pw-dump'))


def props(n):
    return n.get('info', {}).get('props', {})


def node(name):
    return next((n for n in graph() if n.get('type') == 'PipeWire:Interface:Node'
                 and props(n).get('node.name') == name), None)


def wait_for(predicate, label):
    end = time.monotonic() + 5
    while time.monotonic() < end:
        result = predicate()
        if result:
            return result
        time.sleep(.05)
    raise AssertionError('Timed out: ' + label)


def port(name, channel, direction):
    key=(name,channel,direction)
    if key in port_cache and not name.endswith(".recorder"):
        return port_cache[key]
    data = graph()
    n = next(n for n in data if n.get('type') == 'PipeWire:Interface:Node'
             and props(n).get('node.name') == name)
    node_id = n['id']
    value=next(n['id'] for n in data if n.get('type') == 'PipeWire:Interface:Port'
                and props(n).get('node.id') == node_id
                and n.get('info', {}).get('direction') == direction
                and (props(n).get('audio.channel') == channel
                     or props(n).get('port.name', '').endswith('_' + channel)))
    port_cache[key]=value
    return value


def connect(out_name, out_channel, in_name, in_channel):
    run('pw-link', str(port(out_name, out_channel, 'output')),
        str(port(in_name, in_channel, 'input')))


def controls(name, values):
    run('pw-cli', 'set-param', str(node(name)['id']), 'Props',
        json.dumps({'params': [x for pair in values.items() for x in pair]}))
    time.sleep(.05)


def play(target, frequencies):
    private = bool(os.environ.get('LINUX_MIXER_NO_SESSION_MANAGER'))
    name = f'{PREFIX}.player{len(processes)}'
    properties = 'node.dont-fallback=true state.restore-props=false state.restore-target=false state.default-volume=1.0'
    if private:properties += f' node.autoconnect=false node.name={name}'
    p = spawn(['pw-play', '--raw', '--target', target, '--rate', '48000',
               '--channels', str(len(frequencies)), '--format', 'f32', '--properties', properties, '-'],
              stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    samples = array.array('f', (.03 * math.sin(2 * math.pi * f * t / 48000)
                              for t in range(48000) for f in frequencies)).tobytes()

    def feed():
        try:
            while not stop.is_set():
                p.stdin.write(samples)
                p.stdin.flush()
        except (BrokenPipeError, OSError):
            pass
        finally:
            try:
                p.stdin.close()
            except (BrokenPipeError, OSError):
                pass
    threading.Thread(target=feed, daemon=True).start()
    if private:
        wait_for(lambda:(node(name) or {}).get('info',{}).get('n-output-ports',0)>=len(frequencies), 'private player ports')
        for c in ('FL','FR')[:len(frequencies)]:connect(name,c,target,c)


def capture(bus, physical=False):
    name = f'{PREFIX}.recorder'
    target = '0' if physical else f'{PREFIX}.' + ('microphone' if bus == 5 else f'b{bus-4}')
    prop = f'node.dont-fallback=true node.name={name}'
    private = bool(os.environ.get('LINUX_MIXER_NO_SESSION_MANAGER'))
    if physical or private:
        prop += ' node.autoconnect=false'
    p = spawn(['pw-record', '--raw', '--target', target, '--properties', prop,
               '--rate', '48000', '--channels', '2', '--format', 'f32', '--latency', '256', '-'],
              stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    selector = selectors.DefaultSelector()
    try:
        if physical or private:
            wait_for(lambda:(node(name) or {}).get('info',{}).get('n-input-ports',0)>=2, 'private reader ports')
            for c in ('FL', 'FR'):
                connect(f'{PREFIX}.bus{bus}.output' if physical else target, c, name, c)
        selector.register(p.stdout, selectors.EVENT_READ)
        data = bytearray()
        end = time.monotonic() + 4
        wanted = 48000 // 2 * 2 * 4
        while len(data) < wanted and time.monotonic() < end:
            if selector.select(.2):
                part = os.read(p.stdout.fileno(), wanted - len(data))
                if not part:
                    break
                data.extend(part)
        assert len(data) == wanted, ('no audio from bus', bus, len(data))
        floats = array.array('f')
        floats.frombytes(data)
        assert all(math.isfinite(x) for x in floats)
        # Discard startup buffers, then inspect both channels independently.
        return list(floats[-19200::2]), list(floats[-19199::2])
    finally:
        p.terminate()
        p.wait(timeout=2)
        selector.close()


def amplitude(samples, frequency):
    n = len(samples)
    windowed = (v * (.5 - .5 * math.cos(2 * math.pi * i / (n-1)))
                * cmath.exp(-2j * math.pi * frequency * i / 48000)
                for i, v in enumerate(samples))
    return abs(sum(windowed)) * 4 / n


def verify(bus, expected, physical=False):
    left, right = capture(bus, physical)
    for samples, frequencies in zip((left, right), expected):
        for f in (111, 151, 191, 231, 271, 333, 444, 555, 666, 777, 888):
            measured = amplitude(samples, f)
            if f in frequencies:
                assert abs(measured - frequencies[f]) < .002, (bus, f, measured, frequencies[f])
            else:
                assert measured < .0015, ('unexpected audio in bus', bus, f, measured)


def main():
    with tempfile.TemporaryDirectory(prefix='mixer-matrix-test-') as directory:
        config = Path(directory) / 'matrix.conf'
        env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
        subprocess.run([str(ROOT/'build/linux-mixer'), '--write-matrix', str(config), '--prefix', PREFIX], env=env, check=True)
        with (Path(directory)/'matrix.log').open('w+') as log:
            spawn(['pipewire', '-c', str(config)], stdout=log, stderr=log)
            try:
                wait_for(lambda: node(f'{PREFIX}.b3'), 'matrix sources')
                for suffix in ('system', 'music', 'aux'):
                    assert props(node(f'{PREFIX}.{suffix}'))['media.class'] == 'Audio/Sink'
                for source in range(5):
                    name = f'{PREFIX}.processed' if source==0 else f'{PREFIX}.hardware{source+1}.processed'
                    feed = f'{PREFIX}.mic{source}.feed'
                    spawn(['pw-loopback', '--name', f'{PREFIX}.mic{source}', '--channels', '1', '--channel-map', '[ MONO ]',
                           '--capture-props', f'media.class=Audio/Sink node.name={feed} node.virtual=true priority.session=0',
                           '--playback-props', f'media.class=Audio/Source node.name={name} node.autoconnect=false node.virtual=true priority.session=0'],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    wait_for(lambda: node(name), 'synthetic microphone')
                    play(feed, ((111,151,191,231,271)[source],))
                for bus in range(8):
                    for source in range(8):
                        for c, channel in enumerate(('FL', 'FR')):
                            out = (f'{PREFIX}.processed' if source==0 else f'{PREFIX}.hardware{source+1}.processed') if source<5 else f'{PREFIX}.input{source-5}.output'
                            connect(out, 'MONO' if source<5 else channel,
                                    f'{PREFIX}.bus{bus}.capture', f'AUX{source*2+c}')
                play(f'{PREFIX}.system', (333, 444))
                play(f'{PREFIX}.music', (555, 666))
                play(f'{PREFIX}.aux', (777, 888))
                verify(5, ({111: .03}, {111: .03}))
                controls(f'{PREFIX}.bus6.capture', {'left:Gain 1': 1, 'right:Gain 1': 1, 'left:Gain 2': 1, 'right:Gain 2': 1, 'left:Gain 5': 1, 'right:Gain 5': 1, 'left:Gain 7': 1, 'right:Gain 7': 1})
                verify(6, ({111: .03,151: .03,271: .03,555: .03}, {111: .03,151: .03,271: .03,666: .03}))
                controls(f'{PREFIX}.bus7.capture', {'left:Gain 6': 1, 'right:Gain 6': 1, 'left:Gain 8': 1, 'right:Gain 8': 1})
                verify(7, ({333: .03, 777: .03}, {444: .03, 888: .03}))
                verify(5, ({111: .03}, {111: .03}))
                controls(f'{PREFIX}.music', {'left:Mult': .25, 'right:Mult': .25})
                verify(6, ({111: .03,151: .03,271: .03,555: .0075}, {111: .03,151: .03,271: .03,666: .0075}))
                controls(f'{PREFIX}.bus6.capture', {'gainL:Mult': 0, 'gainR:Mult': 0})
                verify(6, ({}, {}))
                verify(0, ({333: .03, 555: .0075, 777: .03}, {444: .03, 666: .0075, 888: .03}), physical=True)
                controls(f'{PREFIX}.bus7.capture', {'left:Gain 3':1,'right:Gain 3':1,'left:Gain 4':1,'right:Gain 4':1})
                verify(7, ({191:.03,231:.03,333:.03,777:.03},{191:.03,231:.03,444:.03,888:.03}))
                print('PASS: 5 mono hardware feeds and 3 stereo app inputs, independent B1/B2/B3 mixes, stereo separation, gain, mute, and A1 without mic monitoring. No audio sent to speakers.')
            except BaseException:
                log.seek(0)
                print(log.read())
                raise


if __name__ == '__main__':
    try:
        main()
    finally:
        stop.set()
        for p in reversed(processes):
            if p.poll() is None:
                p.terminate()
        for p in processes:
            try:
                p.wait(timeout=2)
            except subprocess.TimeoutExpired:
                p.kill()
