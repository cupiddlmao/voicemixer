Voicemixer 0.5.1 is a stability release on top of 0.5.0. It adds no features and
changes no controls; it removes three ways the audio graph could be damaged or
left behind when it is torn down or rebuilt.

## Changes
- **Processor teardown safety.** The bundled LADSPA processors now clear their port
  pointers in `deactivate()`. A host may keep an instance alive after deactivating it,
  and a flush during graph destruction then read buffers the graph had already
  released. `run()` also ignores a null instance, a zero-frame flush and calls whose
  audio ports were never connected. This is the source of the crackle and the rare
  crash when stopping or restarting the engine.
- **One routing graph at a time.** Stopping the mixer now waits for the routing graph
  process to exit before discarding its node and port bookkeeping, with a forced kill
  only as a last resort. Previously the controller could return while the old context
  was still being destroyed and start a second instance against a half-dead one.
- **Recorder playback links no longer stall.** New `pw-link` requests are queued while
  the linker is busy instead of being dropped when the queue is cleared; nothing else
  refreshes that graph at the moment they are produced.
- **Release labelling.** `packaging/release.py` reads the version from the CMake project
  and refuses to run when the Arch `PKGBUILD` disagrees, and CPack uses the project
  version instead of a literal. A tree can no longer publish archives under a version it
  does not build. This is how the 0.4.1-labelled archives in an earlier checkout were cut
  from 0.5.0 source.
- **Tests.** Added a `recorder-links` case for the queued-link behaviour and the
  teardown, zero-frame and unconnected-port cases for the processors.

## Downloads
Assets are produced by `python3 packaging/release.py` and uploaded to this release:
`linux-mixer-0.5.1-ubuntu26.04-x86_64.deb`, `linux-mixer-0.5.1-ubuntu26.04-friend.zip`,
`linux-mixer-0.5.1-arch-friend.zip`, `linux-mixer-0.5.1-gentoo-friend.zip` and
`linux-mixer-0.5.1-source.tar.gz`, with `SHA256SUMS`. Verify against `SHA256SUMS`
before installing.

## Keep your settings
Install over the previous version using the same prefix. Quit the controller first
(**Menu → Quit mixer and stop audio**, or `linux-mixer --quit-running`); closing the
window only hides it. Settings, devices, routes, app assignments, recorder options and
plugin racks are retained. See [updating without losing settings](UPDATING.md).

Because the processor library is loaded by the audio graph rather than by the
controller, an already-running engine keeps the previous processors until the engine is
restarted once after the update.

## Validation and limits
Passed the eight-case `ctest` suite on the 0.5.1 build (PipeWire 1.6.11,
Qt 6.10.2), including the new teardown and queued-link cases, and `--version` reports
0.5.1. On 0.5.1 the Arch kit (makepkg, ctest, pacman install) and the Gentoo kit
(build.sh check/build/test/install) were additionally validated in clean Arch
and Gentoo stage3 containers, without an audio session; a desktop install of 0.5.1 on a
different machine has not been validated. The 0.5.0 validation record still applies to
everything except the three fixes above.

Unchanged from 0.5.0: no VBAN, ASIO, MIDI, macro control, external FX inserts, surround
upmix or parametric master EQ; the bus limiter is a sample-peak limiter, not oversampled
true-peak; a standalone PulseAudio server is not supported.
