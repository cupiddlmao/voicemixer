#!/usr/bin/env python3
"""Photograph the JUCE plugin rack without opening a window or touching audio.

The rack runs in its own process (linux-mixer-plugin-host) and draws itself with
JUCE, so the Qt console's --screenshot path cannot reach it. Ask the host for
--render-ui, which paints the component offscreen and writes raw ARGB, then
convert it here. No display, no compositor screenshot, no PipeWire node.

  ./tests/shot_rack.py build out.png

Exit 0 and the output path on success.
"""
import os
import struct
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip())
        return 2
    build, out = sys.argv[1], sys.argv[2]
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
