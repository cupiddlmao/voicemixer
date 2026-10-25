# Input plugin racks

Each of the five hardware strips and three virtual input strips has a **Plugins**
button. Start the engine, click that button, then **Add VST3 / LADSPA**. Select a
Linux `.vst3` bundle or a compatible stereo LADSPA `.so` library. The rack provides
plugin editor windows, ordering, per-plugin bypass and removal. Closing its window
hides it; processing continues. Right-click the strip’s Plugins button to bypass
the entire rack; click it again to enable the rack. Remove all plugins for an empty passthrough rack.

The insert runs after the strip's gate/denoiser/color/EQ/fader/mute and before its
A/B routing, sends and bus limiter. Mono microphones enter the rack as dual mono;
plugins can produce stereo output (including widening). Master balance and stereo/
mono controls still apply. Composite pre-fader patches and pre-fader recording
intentionally use the original signals. Existing input meters show the signal
before the insert. Effects can add their own latency. The host reports their
combined latency to PipeWire; this is not a DAW with full parallel-chain delay
compensation. Editing/loading/saving a rack can briefly use dry audio rather than
block an audio callback.

Each rack runs in a separate process. A host crash does not terminate the mixer.
The controller restores the original input links after it detects the failed host;
there may be a short audio interruption during reconnection. Reopen **Plugins** to
retry. A plugin that crashes during loading is skipped on the next attempt.
Plugin settings live under `plugin-racks/inputN/rack.json` beside `settings.json`;
copy that directory too when backing up the profile. Installed Linux Mixer uses
`~/.config/PersonalAudio/Linux Mixer/`. Logs are in each rack's `host.log`.

## Windows plugins (including Polyverse Wider)

The host accepts **Linux VST3** plugins, including Linux wrappers produced by
[yabridge](https://github.com/robbert-vdh/yabridge). It cannot load a Windows DLL or
Windows VST3 bundle directly. Native VST2, CLAP, AU and AAX hosting are not included
in this first implementation.

1. Install a mutually compatible Wine/yabridge version pair. Consult yabridge's
   current compatibility notes before using a rolling-distribution Wine upgrade.
   Wine and yabridge are optional dependencies, not bundled with Voicemixer.
2. Run the plugin's official Windows installer in your chosen Wine prefix. For
   Wider, choose its **64-bit VST3** component. Obtain your own installer from
   [Polyverse](https://polyversemusic.com/products/wider/).
3. Add that prefix's Windows VST3 directory to yabridge and generate wrappers:

   ```bash
   yabridgectl add "$HOME/.wine/drive_c/Program Files/Common Files/VST3"
   yabridgectl sync
   ```

   Use the matching directory if you installed in a different Wine prefix.
4. In the input's rack, select the Linux wrapper under `~/.vst3/yabridge/`.
   Do not select the original Windows file inside the Wine prefix.
5. Open the plugin editor and configure it. To hear widening, keep the destination
   bus in stereo. A voice/chat application may downmix its microphone input to mono.

Compatibility depends on the plugin, its installer/DRM and the Wine/yabridge pair.
The native VST3 host is covered by an automated stereo audio test. Wider 2.0.0
Windows VST3 was also tested locally through Wine Staging 11.20 and official
yabridge development build 5.1.1-57-gb580a9f7. Stable yabridge 5.1.1 is not the
version used for that verification. When a user-installed bridge exists under
`~/.local/share/yabridge`, the mixer prepends that directory to its rack workers’
PATH so the Linux wrapper and Windows helper come from the same installation.

## Building and testing

The isolated host is enabled by `BUILD_PLUGIN_HOST=ON` (default). It uses vendored
JUCE under GPLv3 and the PipeWire development library. Linux GUI headers and
FreeType are needed at build time. Set `-DBUILD_PLUGIN_HOST=OFF` to build the mixer
without the optional host. `python3 tests/verify_plugins.py` runs actual synthetic
stereo audio through a locally built VST3 fixture; it requires the desktop session
but never connects test audio to physical devices. The fixture is not installed.

## Wider editor on Linux

For Wider, **Open editor** opens host-side Width (0–200%), Low Bypass, Power,
and Mono controls. These operate the actual plugin parameters and do not rely on
Wine’s embedded mouse input. **Parameters** opens the generic parameter editor.
**Plugin UI** requests the original Windows editor; its mouse/scaling behavior
may depend on Wine and the desktop, so the host-side controls are preferred.
A local opt-in test (`verify-wider-controls`) checks real Windows Wider DSP at
0%, 100%, and 200%, and confirms saved-state restoration. No mic is recorded.

## Auto-Tune Pro controls on Linux

For Auto-Tune Pro, **Open editor** opens Voicemixer's native control panel for
Retune Speed, Flex-Tune, Humanize, Natural Vibrato, Tracking, formant/throat,
transpose, detune, vibrato rate, mix, input type, key, scale, correction mode,
Classic DSP, formant correction and bypass. The panel controls the actual
installed plugin; it does not replace its pitch-correction engine. Existing rack
settings are retained. The scale selector follows the Modern/Classic DSP switch.

**Plugin UI** still opens Antares' original editor, which may stall under Wine.
Graph editing requires that original editor. **Parameters** exposes the remaining
plugin parameters. Native controls autosave normally; only the original editor
defers automatic saves while it is visible.

The opt-in `verify-autotune-controls` test uses a separately installed real plugin
and synthetic audio. It checks pointer dragging, parameter updates, key and scale
selection, external updates, and saved-state restoration. It does not record a mic.
Build it with `cmake --build build --target verify-autotune-controls`, then run it
with the Linux wrapper path and an output PNG path as its two arguments. Pointer
injection is enabled only with `VOICEMIXER_PRIVATE_POINTER_TEST=1` and should only
be used on a private test display.

A local `plugin-runtime.json` beside settings.json may specify `binDirectory`,
`bridgeDirectory`, `WINELOADER`, and `WINESERVER` to pin audio plugins to a
compatible runtime without changing the system Wine installation.

Enabled racks load audio processing in the background at startup without creating
a rack window. The window is created only when you click **Plugins**.

Click **Plugins** again to hide or reopen the same rack without bypassing its
effects. The rack also has a **Hide rack** button. Closing its window keeps
processing active; right-clicking the strip’s Plugins button bypasses the rack.

Unconnected auxiliary plugin inputs receive silence while the main stereo pair
is processed. Auxiliary outputs are not routed to separate mixer buses. Plugins
with more than 16 total internal channels are rejected.

Original plugin editors defer automatic state snapshots while visible to avoid blocking their controls. Close the editor window to save changes promptly; quitting Voicemixer also saves the rack. A crash while the original editor is open can lose changes made since its last save.
