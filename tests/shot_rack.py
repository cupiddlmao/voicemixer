#!/usr/bin/env python3
"""Photograph the JUCE plugin rack without opening a window or touching audio.

The rack runs in its own process (linux-mixer-plugin-host) and draws itself with
JUCE, so the Qt console's --screenshot path cannot reach it. Ask the host for
--render-ui, which paints the component offscreen and writes raw ARGB, then
convert it here. No display, no compositor screenshot, no PipeWire node.

  ./tests/shot_rack.py build out.png
  ./tests/shot_rack.py build out.png --populated   # a real rack with plugins loaded

--bypassed marks those slots bypassed so the engaged toggle state is visible.

--populated loads the test VST3 fixture into the rack, because a design judged
only against an empty combo tends to fall apart once a plugin is present. The
fixture is scanned with the host's own --probe, so the rack state is written in
exactly the format the app saves.

Exit 0 and the output path on success.
"""
import json
import os
import struct
import subprocess
import sys
import tempfile


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    populated = "--populated" in sys.argv
    bypassed = "--bypassed" in sys.argv
    if len(argv) != 2:
        print(__doc__.strip())
        return 2
    build, out = argv[0], argv[1]
    host = os.path.join(build, "linux-mixer-plugin-host")
    if not os.path.exists(host):
        print(f"missing {host} - build the linux-mixer-plugin-host target first")
        return 1
    from PIL import Image

    expected = 8 + 850 * 190 * 4
    with tempfile.TemporaryDirectory() as work:
        raw = os.path.join(work, "rack.argb")
        # A throwaway rack folder keeps this out of the user's real rack state.
        folder = os.path.join(work, "rack")
        os.mkdir(folder)
        if populated:
            fixture = os.path.join(build, "tests", "MixerTestGain.vst3")
            if not os.path.exists(fixture):
                print(f"missing {fixture} - build the mixer_test_vst3 target first")
                return 1
            # Ask the host itself for the plugin description, then store two
            # slots so the rack renders a populated list rather than a placeholder.
            probe = subprocess.run([host, "--probe", fixture], capture_output=True,
                                   text=True, timeout=120)
            descriptions = json.loads(probe.stdout)
            if not descriptions:
                print("--probe found no plugin types in the fixture")
                return 1
            json.dump({"plugins": [
                {"description": d, "name": "Mixer Test Gain", "state": "", "bypass": bypassed}
                for d in (descriptions * 2)[:2]
            ]}, open(os.path.join(folder, "rack.json"), "w"))
        # No PipeWire is reachable in this environment on purpose: render-only
        # mode never creates a filter, so the live graph is untouched.
        env = dict(os.environ, XDG_RUNTIME_DIR=work)
        proc = subprocess.run(
            [host, folder, "linux_mixer.shotrack", "Hardware input 1",
             "--render-ui", raw],
            env=env, capture_output=True, text=True, timeout=60,
        )
        if not os.path.exists(raw):
            print("the host wrote no capture:")
            print(proc.stdout.strip() or proc.stderr.strip() or "(no output)")
            return 1
        size = os.path.getsize(raw)
        if size != expected:
            print(f"capture is {size} bytes, expected {expected}")
            return 1
        data = open(raw, "rb").read()
        w, h = struct.unpack("<ii", data[:8])
        Image.frombytes("RGBA", (w, h), data[8:]).convert("RGB").save(out)
    print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
