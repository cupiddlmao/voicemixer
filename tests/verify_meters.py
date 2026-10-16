#!/usr/bin/env python3
"""Check live meter isolation and stable stereo links; never plays or changes audio."""
import json
import subprocess
import time


def snapshot():
    graph = json.loads(subprocess.check_output(['pw-dump']))
    nodes = {}
    ports = {}
    links = set()
    for item in graph:
        info = item.get('info', {})
        props = info.get('props', {})
        if item['type'] == 'PipeWire:Interface:Node':
            nodes[props.get('node.name')] = (item['id'], props)
        elif item['type'] == 'PipeWire:Interface:Port':
            ports[item['id']] = (props.get('node.id'), info.get('direction'), props.get('audio.channel'))
        elif item['type'] == 'PipeWire:Interface:Link':
            links.add((info['output-port-id'], info['input-port-id']))
    serials = {}
    for i in range(11):
        name = f'linux_mixer.meter.input{i}' if i < 3 else f'linux_mixer.meter.bus{i-3}'
        source = ['linux_mixer.system', 'linux_mixer.music', 'linux_mixer.aux'][i] if i < 3 else (f'linux_mixer.bus{i-3}.output' if i < 8 else ['linux_mixer.microphone', 'linux_mixer.b2', 'linux_mixer.b3'][i-8])
        assert name in nodes, f'Missing meter: {name}'
        node_id, props = nodes[name]
        assert props.get('node.autoconnect') is False, f'Policy can redirect {name}'
        assert props.get('state.restore-target') is False, f'Stale target can be restored: {name}'
        for channel in ('FL', 'FR'):
            inputs = [p for p, v in ports.items() if v == (node_id, 'input', channel)]
            assert len(inputs) == 1, f'Missing {channel} input: {name}'
            incoming = [out for out, dest in links if dest == inputs[0]]
            assert len(incoming) == 1, f'Missing/extra {channel} connection: {name}'
            assert ports[incoming[0]] == (nodes[source][0], 'output', channel), f'Wrong source: {name}'
        serials[name] = props['object.serial']
    return serials


first = snapshot()
time.sleep(3)
assert snapshot() == first, 'A meter reader restarted instead of staying connected'
print('PASS: all 11 meters have stable, private stereo connections to their intended sources, including AUX and B1.')
