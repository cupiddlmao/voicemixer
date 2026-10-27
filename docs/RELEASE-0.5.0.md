Voicemixer 0.5.0 preview adds command-line control, a front/back position cue, complete profile backup/restore, a clearer console and more stable meters and recorder timing.

## Changes
- **Command-line control:** `linux-mixer --ctl mute|gain|bypass|denoise <target> [value]` changes a running mixer (mic1–5, system/music/aux, A1–A5, B1–B3), suitable for desktop keyboard shortcuts. See the README.
- **Position pad front/back:** the hardware input Position pad now also moves an input forward (up to +3 dB) or back (down to −9 dB dry, with reverb/delay sends unchanged). This is a stereo distance cue, not true surround.
- **Complete profile backup/restore:** one JSON file bundles mixer settings, plugin chains and saved plugin states; restore reviews missing devices and plugins, supports remapping and keeps a rollback backup.
- **Console readability:** larger, higher-contrast labels, separate DENOISE/PROCESS and COMP/GATE captions, A buses green and B buses amber, first-run guide, and a live driver sample-rate/period button that opens Audio health.
- **Smoother effects:** reverb room, damping and echo feedback changes now ramp over 10 ms.
- **Stability:** meter and recorder capture readers keep draining off the interface thread, with bounded overflow reporting and tests.
- **CI:** a GitHub Actions workflow builds and runs the native tests.

## Downloads
- **Arch/CachyOS:** `linux-mixer-0.5.0-arch-friend.zip` — matching source, checksummed PKGBUILD, INSTALL.txt and UPDATING.md. Extract and run `makepkg -si` as your normal user.
- **Gentoo/KDE:** `linux-mixer-0.5.0-gentoo-friend.zip` — source, dependency/PipeWire tutorial and build.sh. Extract to a new folder and run `bash build.sh --install`.
- **Ubuntu:** no `.deb` is attached to this release. Build from the source tarball using the README's Ubuntu instructions, or run `python3 packaging/release.py` on Ubuntu 26.04 to produce one.
- **Other source builds:** `linux-mixer-0.5.0-source.tar.gz` — see README.md.
- Verify assets using `SHA256SUMS`.

## Keep your settings
Quit the mixer controller (`linux-mixer --quit-running`; verify it exited) and back up `~/.config/PersonalAudio/Linux Mixer/` before updating. Install over the program with the same prefix. Settings are retained; old settings open with neutral front/back position.

## Validation and limits
Native tests (DSP, profiles, control commands, meter/recorder readers, history, stability/update checks) pass locally. The `--ctl` command, new console text and position cue have been built and checked in offscreen screenshots and unit tests; the position cue and reverb smoothing have not been evaluated by ear beyond the author's own use. The Python/PipeWire/Xvfb checks were not rerun for this release.

No `.deb` is provided because it must be built on Ubuntu to link the right Qt libraries and this release was built on CachyOS. Clean Arch, Gentoo and Ubuntu installations have not been validated. Audio health may show errors on some hardware; it reports PipeWire's counters and is not a count of audible clicks. Standalone PulseAudio is not supported.
