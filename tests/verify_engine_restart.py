#!/usr/bin/env python3
"""Start an engine on a private server and assert the routing graph comes up fast.

A restart used to discard the routing matrix's first launch — a redundant
second Engine::start() (the `--start-engine` timer racing the early recovery
path) landed between the matrix launch and its first publish, and the matrix's
10 s launch backoff then delayed the graph by a full retry interval (measured
10.9 s). The signature guard makes the redundant start a no-op, and a deliberate
stop clears the backoff, so the graph is expected within a couple of refresh
intervals; this check fails long before the old behaviour's 11 s.

Runs on its own PipeWire server with a synthetic microphone, a temporary state
directory and no session manager, so the desktop's audio session is never used
and no audio reaches speakers.
"""
import os
import subprocess
import tempfile
import time
from pathlib import Path

import verify_matrix as m
from verify_macros import CONTROLLER, MIC_NAME, wait_for, write_state


def main():
    with tempfile.TemporaryDirectory(prefix='mixer-restart-test-') as tmp:
        base = Path(tmp)
        runtime = base / 'runtime'
        runtime.mkdir(mode=0o700)
        state = base / 'state'
        state.mkdir()
        env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), PIPEWIRE_RUNTIME_DIR=str(runtime),
                   QT_QPA_PLATFORM='offscreen', PULSE_SERVER='unix:' + str(runtime / 'no-pulse'),
                   LINUX_MIXER_STATE_DIR=str(state), LINUX_MIXER_NO_SESSION_MANAGER='1')
        os.environ.update(env)
        write_state(state, MIC_NAME)
        log = (base / 'restart.log').open('w+')
        procs = []
        try:
            m.spawn(['pipewire'], env=env, stdout=log, stderr=log)
            end = time.monotonic() + 5
            while not (runtime / 'pipewire-0').exists():
                assert time.monotonic() < end, 'private PipeWire did not start'
                time.sleep(.05)

            # A synthetic microphone so the engine's mic capture chain exists
            # without a real device.
            m.spawn(['pw-loopback', '--name', f'{m.PREFIX}.restart.feed', '--channels', '1',
                     '--channel-map', '[ MONO ]',
                     '--capture-props', f'media.class=Audio/Sink node.name={m.PREFIX}.restart.feed node.virtual=true priority.session=0',
                     '--playback-props', f'media.class=Audio/Source node.name={MIC_NAME} node.autoconnect=false node.virtual=true priority.session=0'],
                    env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            wait_for(lambda: m.node(MIC_NAME), 'synthetic microphone')

            t0 = time.monotonic()
            procs.append(m.spawn([str(CONTROLLER), '--start-engine'], env=env,
                                 stdout=log, stderr=log))
            wait_for(lambda: m.node('linux_mixer.capture') and m.node('linux_mixer.music'),
                     'engine and routing graphs', timeout=8)
            elapsed = time.monotonic() - t0
            assert elapsed < 8, ('the routing graph took a full launch backoff to appear', elapsed)
            time.sleep(1)
        finally:
            for p in procs:
                try:
                    p.terminate()
                    p.wait(3)
                except Exception:
                    pass
    print(f'PASS: engine start with a mic brought up the routing graph in {elapsed:.2f} s '
          'on a private server (the discarded-launch behaviour measured 10.9 s). No speakers.')


if __name__ == '__main__':
    main()
