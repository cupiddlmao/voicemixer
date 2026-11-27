#!/usr/bin/env python3
"""Run a macro on a private engine and read the applied values back from the graph.

The controller runs on its own PipeWire server with a synthetic microphone, a
temporary state directory and no session manager, so the desktop's audio session
is never used and no audio reaches speakers. The values a macro writes are read
from `pw-dump`, the same way verify_matrix.py reads the graph, so this is the
check that a macro reaches the audio graph as one update rather than one cycle
per step.
"""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

import verify_matrix as m

ROOT = Path(__file__).resolve().parents[1]
CONTROLLER = ROOT / 'build' / 'linux-mixer'
MIC_NAME = f'{m.PREFIX}.macro.mic'
GRACE = 1.5          # a 700 ms ctl poll, the 20 ms apply, and pw-cli startup
SETTLE = .3          # the burst of pw-cli writes one apply() issues


def wait_for(predicate, label, timeout=5):
    """verify_matrix.wait_for with a per-call deadline (its own is fixed at 5 s)."""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        result = predicate()
        if result:
            return result
        time.sleep(.05)
    raise AssertionError('Timed out: ' + label)


def controls_of(node):
    """A node's applied filter-chain controls, from its Props params list."""
    controls = {}
    for params in node.get('info', {}).get('params', {}).get('Props', []):
        flat = params.get('params', [])
        controls.update(zip(flat[::2], flat[1::2]))
    return controls


def snapshot():
    """One graph dump: mic 1's fader and the music input's matrix gain."""
    values = {}
    for n in m.graph():
        if n.get('type') != 'PipeWire:Interface:Node':
            continue
        name = m.props(n).get('node.name')
        if name in ('linux_mixer.capture', 'linux_mixer.music'):
            values[name] = controls_of(n)
    mic = values.get('linux_mixer.capture', {})
    music = values.get('linux_mixer.music', {})
    return mic.get('fader:Mult'), music.get('left:Mult'), music.get('right:Mult')


def read_state(directory):
    return json.loads((Path(directory) / 'settings.json').read_text())


def write_state(directory, mic):
    # Two macros: a valid two-step one, and one whose second step names a
    # missing target, so the loader must refuse that macro as a unit.
    (Path(directory) / 'settings.json').write_text(json.dumps({
        'version': 3, 'engineEnabled': True, 'mic': mic,
        'macros': [
            {'name': 'Stream', 'steps': ['gain mic1 -6', 'mute music on']},
            {'name': 'Half', 'steps': ['gain mic1 -6', 'gain mic9 -6']},
        ]}))


def ctl(env, *words):
    return subprocess.run([str(CONTROLLER), '--ctl', *words], env=env,
                          capture_output=True, text=True, timeout=10)


def queue_files(env):
    return list((Path(env['LINUX_MIXER_STATE_DIR']) / '.runtime' / 'ctl').glob('*.cmd'))


def main():
    with tempfile.TemporaryDirectory(prefix='mixer-macro-test-') as tmp:
        base = Path(tmp)
        runtime = base / 'runtime'
        runtime.mkdir(mode=0o700)
        state = base / 'state'
        state.mkdir()
        env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), PIPEWIRE_RUNTIME_DIR=str(runtime),
                   QT_QPA_PLATFORM='offscreen', PULSE_SERVER='unix:' + str(runtime / 'no-pulse'),
                   LINUX_MIXER_STATE_DIR=str(state), LINUX_MIXER_NO_SESSION_MANAGER='1')
        # verify_matrix's helpers read this ambient environment.
        os.environ.update(env)
        write_state(state, MIC_NAME)
        assert [x['name'] for x in read_state(state)['macros']] == ['Stream', 'Half']
        log = (base / 'macros.log').open('w+')
        controller = None
        try:
            m.spawn(['pipewire'], env=env, stdout=log, stderr=log)
            end = time.monotonic() + 5
            while not (runtime / 'pipewire-0').exists():
                assert time.monotonic() < end, 'private PipeWire did not start'
                time.sleep(.05)

            # A synthetic microphone for the engine to open, so mic 1's chain and
            # its fader exist without a real device.
            m.spawn(['pw-loopback', '--name', f'{m.PREFIX}.macro.feed', '--channels', '1',
                     '--channel-map', '[ MONO ]',
                     '--capture-props', f'media.class=Audio/Sink node.name={m.PREFIX}.macro.feed node.virtual=true priority.session=0',
                     '--playback-props', f'media.class=Audio/Source node.name={MIC_NAME} node.autoconnect=false node.virtual=true priority.session=0'],
                    env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            wait_for(lambda: m.node(MIC_NAME), 'synthetic microphone')

            controller = m.spawn([str(CONTROLLER), '--start-engine'], env=env, stdout=log, stderr=log)
            # The engine's own start() tears the matrix down, so the matrix's
            # first launch (during that teardown) is discarded and its 10 s
            # retry backoff has to elapse before the graph comes up for real.
            wait_for(lambda: m.node('linux_mixer.capture') and m.node('linux_mixer.music'),
                     'engine graph on the private server', timeout=40)
            baseline = wait_for(lambda: (lambda v: v if v[0] is not None and v[1] is not None else None)(snapshot()),
                                'applied baseline controls')
            assert baseline[0] == 1.0, ('mic 1 fader starts at unity', baseline)
            assert baseline[1] == 1.0 and baseline[2] == 1.0, ('music input starts unmuted', baseline)

            # A macro whose second step is invalid is refused at load, so the CLI
            # never queues it: the valid first step must not be applied.
            refused = ctl(env, 'macro', 'Half')
            assert refused.returncode == 2, ('a malformed macro was accepted by --ctl', refused)
            assert 'unknown macro' in refused.stderr, refused.stderr
            assert not queue_files(env), 'a refused macro was queued anyway'
            time.sleep(.9)
            after_refusal = snapshot()
            assert after_refusal == baseline, ('a refused macro changed the graph', baseline, after_refusal)

            # A stale queue file naming that macro is refused by the controller
            # itself on the next poll: nothing is applied and the queue is cleared.
            (state / '.runtime' / 'ctl').mkdir(parents=True, exist_ok=True)
            (state / '.runtime' / 'ctl' / 'zz-stale.cmd').write_text('macro Half')
            wait_for(lambda: not queue_files(env), 'stale macro command drained')
            time.sleep(.3)
            after_stale = snapshot()
            assert after_stale == baseline, ('a stale macro command changed the graph', baseline, after_stale)

            # The positive case: one trigger, both steps in one graph update.
            triggered = time.monotonic()
            accepted = ctl(env, 'macro', 'Stream')
            assert accepted.returncode == 0, ('--ctl macro Stream was refused', accepted.stderr)
            polled = None
            first = None
            landed = None
            while time.monotonic() - triggered < GRACE * 2:
                fader, left, right = snapshot()
                now = time.monotonic() - triggered
                if polled is None and not queue_files(env):
                    polled = now
                gain_changed = fader is not None and abs(fader - 1.0) > 1e-3
                mute_changed = left is not None and left < .5 and right < .5
                if first is None and (gain_changed or mute_changed):
                    first = (now, gain_changed, mute_changed)
                if gain_changed and mute_changed:
                    landed = now
                    break
            assert first is not None, 'the macro never reached the graph'
            assert landed is not None, ('only one of the two steps landed', first)
            assert first[1] and first[2], ('the steps landed in separate updates', first)
            assert polled is not None, 'the ctl queue was never drained'
            assert landed <= polled + SETTLE, ('the macro needed a later apply cycle', polled, landed)
            assert landed <= GRACE, ('the macro missed the first poll window', landed)
            fader, left, right = snapshot()
            assert abs(fader - 10 ** (-6 / 20)) < 1e-3, ('mic 1 gain not applied', fader)
            assert left == 0 and right == 0, ('music input not muted', left, right)
            saved = read_state(state)
            assert saved['hardware'][0]['gain'] == -6 and saved['routing']['inputMute'][1], \
                ('the applied macro was not saved', saved)

            # Running a macro twice flips a toggle; a gain+mute macro is stable,
            # so a second trigger must land on the same values.
            again = ctl(env, 'macro', 'Stream')
            assert again.returncode == 0, again.stderr
            time.sleep(.9)
            assert snapshot() == (fader, left, right), 'a second run changed a non-toggle macro'

            print(f'PASS: macro "gain mic1 -6" + "mute music on" applied as one update '
                  f'({landed * 1000:.0f} ms after the trigger) and read back from the graph; '
                  f'a macro whose second step fails was refused at load and from a stale queue '
                  f'command without applying its first step. Private server, no speakers.')
        finally:
            if controller is not None and controller.poll() is None:
                (state / '.runtime' / 'quit.request').write_text('stop')
                try:
                    controller.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    controller.terminate()
                    controller.wait(timeout=5)
            # Only graphs this test's controller launched, verified by the state
            # path in their command line, exactly as verify_stability_private does.
            for kind in ('engine', 'matrix'):
                try:
                    pids = [int(t) for t in (state / '.runtime' / f'{kind}.pid').read_text().split()]
                except (FileNotFoundError, ValueError):
                    pids = []
                for pid in pids:
                    try:
                        if str(state).encode() in Path(f'/proc/{pid}/cmdline').read_bytes():
                            os.kill(pid, signal.SIGTERM)
                    except (FileNotFoundError, ProcessLookupError, PermissionError):
                        pass
            m.stop.set()
            for p in reversed(m.processes):
                if p.poll() is None:
                    p.terminate()
            for p in m.processes:
                try:
                    p.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    p.kill()
            log.close()


if __name__ == '__main__':
    main()
