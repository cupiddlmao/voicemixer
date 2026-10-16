#!/usr/bin/env python3
"""Explicit live crash-recovery check. Briefly interrupts mixer audio; settings unchanged."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import time
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--live',action='store_true',required=True);parser.parse_args()

def wait(predicate,label):
    end=time.monotonic()+18
    while time.monotonic()<end:
        value=predicate()
        if value:return value
        time.sleep(.2)
    raise AssertionError('Timed out: '+label)

def graph():return json.loads(subprocess.check_output(['pw-dump'],timeout=4))
def props(n):return n.get('info',{}).get('props',{})
def nodes():return {props(n).get('node.name'):n for n in graph() if n.get('type')=='PipeWire:Interface:Node'}
def pid(kind):
    try:return int((ROOT/f'.runtime/{kind}.pid').read_text())
    except (FileNotFoundError,ValueError):return 0
def terminate(kind):
    number=pid(kind);cmd=Path(f'/proc/{number}/cmdline').read_bytes().split(b'\0')
    conf='microphone' if kind=='engine' else 'matrix'
    assert Path(os.fsdecode(cmd[0])).name=='pipewire' and str(ROOT/f'.runtime/{conf}.conf').encode() in cmd
    os.kill(number,signal.SIGKILL);return number

before=json.loads((ROOT/'settings.json').read_text())
assert before.get('engineEnabled'), 'Run the mixer controller and engine first'
old=terminate('matrix')
wait(lambda:pid('matrix')!=old and all(name in nodes() for name in ('linux_mixer.system','linux_mixer.music','linux_mixer.aux','linux_mixer.b2','linux_mixer.b3')),'routing process recovery')
old=terminate('engine')
wait(lambda:pid('engine')!=old and 'linux_mixer.capture' in nodes() and 'linux_mixer.microphone' in nodes(),'mic process and matrix recovery')
time.sleep(2)
after=json.loads((ROOT/'settings.json').read_text());assert after==before,'recovery changed saved settings'
data=graph();named={props(n).get('node.name'):n['id'] for n in data if n.get('type')=='PipeWire:Interface:Node'}
ports={n['id']:props(n).get('node.id') for n in data if n.get('type')=='PipeWire:Interface:Port'}
assert any(ports.get(n.get('info',{}).get('output-port-id'))==named[before['mic']] and ports.get(n.get('info',{}).get('input-port-id'))==named['linux_mixer.capture'] for n in data if n.get('type')=='PipeWire:Interface:Link'),'selected mic link missing'
print('PASS: routing and microphone process crashes recovered, selected mic link returned, all saved settings and app rules unchanged.')
