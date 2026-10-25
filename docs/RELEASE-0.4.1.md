Voicemixer 0.4.1 preview improves plugin stability, startup behavior, and audio diagnostics, with native Auto-Tune Pro controls and a built-in update checker.

## Changes
- Input plugin racks on all five hardware and three virtual input strips, with native VST3/stereo LADSPA hosting and optional Windows VST3 via Wine/yabridge.
- Racks start processing in the background without creating a window. Click Plugins to open or hide a rack; effects keep running.
- Native Auto-Tune Pro controls for retune speed, input type, key, Modern/Classic scales, Flex-Tune, Humanize, vibrato, formant, transpose, detune, mix and bypass. Existing plugin settings are preserved.
- Native Wider controls remain available. Plugin UI still opens the original vendor editor.
- Correct auxiliary-channel buffers for plugins such as Auto-Tune, improved state saving, bounded control commands, and watchdog recovery to normal input routing when a plugin worker hangs.
- Audio health uses a continuous PipeWire reader so startup/idle placeholder values do not repeatedly count old errors. Real new increments are still counted.
- Manual update checking, with optional startup checks and preview-channel selection. It downloads release metadata only; installation remains manual.
- Launch the installed mixer with `voicemixer` from a terminal.
- Explicit quit requests exit even when the normal close button is configured to hide to the tray.
- Includes live balance/position controls, Color Panel/Position switching, and recorder/routing features from the 0.4 development series.

## Downloads
- **Arch/CachyOS:** `linux-mixer-0.4.1-arch-friend.zip` — matching source, checksummed PKGBUILD, INSTALL.txt and UPDATING.md. Extract and run `makepkg -si` as your normal user.
- **Gentoo/KDE:** `linux-mixer-0.4.1-gentoo-friend.zip` — matching source, dependency/PipeWire tutorial, build.sh and UPDATING.md. Extract to a new folder and run `bash build.sh --install` after following INSTALL.txt.
- **Ubuntu and other source builds:** `linux-mixer-0.4.1-source.tar.gz` — see README.md for dependencies and build/install instructions. This release does not provide a new Ubuntu .deb or universal Linux binary.
- Verify assets using `SHA256SUMS`.

## Keep your settings
Quit the mixer controller and back up `~/.config/PersonalAudio/Linux Mixer/` before updating. If an older build only hides after `linux-mixer --quit-running`, run it again and verify it has exited before replacing executables. Install over the program using the same prefix; keep the profile folder. Devices, routes, levels, processing settings, app assignments, recorder options, plugin racks and login startup settings are retained. Both ZIPs include detailed UPDATING.md instructions, including migration from a source-folder launch.

## Validation and limits
Passed native DSP and stability/update tests, private PipeWire/Xvfb startup and actual rack-button mouse tests, worker hang recovery, and stereo plugin-chain/state tests. The native Auto-Tune panel was tested against the real Windows plugin with synthetic audio, a real pointer drag, parameter/scale changes and saved-state restoration. The continuous health reader was checked against live PipeWire with unchanged counters.

Antares' original Windows editor can still be laggy under Wine; use Open editor for the native controls. Graph editing requires Plugin UI. Plugin compatibility and added latency vary. Windows plugins, Wine prefixes, credentials, personal settings and recordings are not included. Each person installs their own plugins and compatible Wine/yabridge runtime. Standalone PulseAudio is not supported; PulseAudio apps work through pipewire-pulse.

This is a preview tested locally on CachyOS. Clean Gentoo and Ubuntu installations have not been validated, and native VST2/CLAP/AU/AAX hosting is not included.
