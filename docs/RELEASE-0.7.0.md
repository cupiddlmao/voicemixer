Voicemixer 0.7.0 adds MIDI control: a hardware controller's knobs and keys are
bound to the same faders, switches and macros the console and the `--ctl` queue
drive, through the ALSA sequencer, with bindings learned from the physical
control and edited in a dialog under Menu → MIDI bindings….

> The validation record below was completed and reviewed before the version bump,
> packaging and tag.

## Changes
- **Bindings.** A binding is one MIDI message (a control change or a note-on)
  mapped to one action: the same `{verb, target, value}` command the `--ctl`
  queue accepts (`mute`, `gain`, `bypass`, `denoise` over the usual targets) or
  the name of a saved macro. Bindings are stored as a list under the `midi` key
  of `settings.json`, beside `macros`, and travel with a complete profile
  bundle, which shape-checks them on import. No migration is needed for
  existing profiles; MIDI is off until enabled in the editor.
- **Mapping.** A CC bound to gain moves the strip across the whole −60..+12 dB
  clamp; a CC bound to mute/bypass/denoise is on above value 64, off below it; a
  note-on runs its stored action as parsed, so `toggle` behaves as a button
  press; a macro triggers above 64. Channel −1 matches any channel. Partial-
  range gain maps, relative encoders and 14-bit pairs do not exist yet; the
  stored shape can gain them without rewriting old files.
- **Reader.** The input side is the ALSA sequencer via `libasound`, already a
  build requirement on every supported distribution — no new package. The
  reader opens a client named **Linux Mixer** with one port named **Linux
  Mixer MIDI** and subscribes to every readable source port, rebuilding the set
  on ports-changed events, so a controller plugged in later is picked up
  without a restart. Events arrive on the reader's own thread and are marshalled
  to the GUI thread; each delivered batch applies with one coalesced
  `engine.apply()`, the same one-update discipline a macro run keeps. No audio
  processing changes: the reader writes existing settings fields through the
  existing apply path, so it adds no DSP risk by construction, and the
  graph-rebuild verb refusal it inherits from macros keeps a binding from
  restarting the engine.
- **Editor and learn mode.** Menu → MIDI bindings… opens a dialog in the
  existing editor style: one row per binding (id, message, channel shown as
  `any` or `0`–`15`, controller/note number, action), New/Delete, a Learn
  button that fills the selected row from the next physical message, a live
  **MIDI enabled** switch that starts and stops the reader, and a status line
  naming the first refusal. Save validates the whole list against the saved
  macros; failures leave `settings.json` untouched and are never dialogs.
- **Triggering.** There is nothing to trigger from a terminal: the reader runs
  while the controller runs (including hidden to the background) and stops only
  on the real quit path. A system without an ALSA sequencer (`/dev/snd/seq`)
  builds and runs normally; MIDI alone is dead, reported on the status line,
  never a dialog.

## Tests
`ctest` gained three cases alongside the existing eleven:
- `midi-bindings` — the model: parsing, persistence round-trips, malformed
  entries dropped individually, duplicate ids refusing the list, validation
  against the saved macros, range refusals.
- `midi-input` — the mapping through its pure functions (all 128 CC positions
  inside the clamp and monotonic, the 64 threshold, velocity-0 ignoring,
  channel matching, multi-binding order), plus a real in-process sequencer
  round trip when `/dev/snd/seq` exists, sending events through a second ALSA
  client and asserting what the reader delivers.
- `midi-editor` — the dialog against a temporary state directory: canonical
  persisted JSON, refusals leaving the stored file byte-identical, the enable
  switch driving the real reader, Learn filled from a sequencer message, and
  `applyMidiBatch()` driven directly for the one-apply-per-batch discipline,
  including the skipped-macro path.

## Downloads
Assets are produced by `python3 packaging/release.py` and uploaded to this release:
`linux-mixer-0.7.0-ubuntu26.04-x86_64.deb`, `linux-mixer-0.7.0-ubuntu26.04-friend.zip`,
`linux-mixer-0.7.0-arch-friend.zip`, `linux-mixer-0.7.0-gentoo-friend.zip` and
`linux-mixer-0.7.0-source.tar.gz`, with `SHA256SUMS`. Verify against `SHA256SUMS`
before installing.

## Keep your settings
Install over the previous version using the same prefix. Quit the controller first
(**Menu → Quit mixer and stop audio**, or `linux-mixer --quit-running`); closing the
window only hides it. Settings, devices, routes, app assignments, recorder options,
plugin racks and macros are retained; MIDI bindings are part of `settings.json` and
travel with a profile bundle. See [updating without losing settings](UPDATING.md).

## Validation and limits

Recorded on the 0.7.0 build (this machine, CachyOS, isolated PipeWire sessions
only — the host desktop session was never touched):

- **ctest:** 14/14 passed — `control-commands`, `complete-profiles`, `dsp`,
  `meter-reader-thread`, `recorder-capture-thread`, `recorder-history`,
  `recorder-links`, `owned-process-ledger`, `stability-and-updates`,
  `macro-commands`, `macro-editor`, `midi-bindings`, `midi-input`,
  `midi-editor`. The Ubuntu 26.04 packaging run repeats the same suite inside
  the container before building the `.deb`.
- **The in-process sequencer round trip ran for real on this machine** (the
  `/dev/snd/seq` skip did not trigger): a second ALSA client opened inside the
  test process, subscribed to the reader's port, and delivered fader
  endpoints, threshold flips, channel matching and macro triggers through
  actual sequencer events.
- **No physical MIDI controller was available**, so the feel of real hardware
  is unverified — see the honest list at the end of
  [MIDI instructions](MIDI.md#verification-and-scope): hotplug has no
  assertion behind it, the console wiring is argued from code rather than
  measured, and a reader that fails after opening is not surfaced to the
  status line mid-session.
- **Environment-blocked, not regressions:** `verify_matrix.py`,
  `verify_eq_fx.py`, `verify_hardware.py`, `verify_position.py` and
  `verify_meters.py` cannot run in a private PipeWire session — WirePlumber
  does not manage devices there, so test nodes expose no ports (same
  environment limitation recorded in 0.6.0, reproduced without mixer code).
  `verify_plugins.py`, `verify_profile_private.py` and
  `verify_stability_private.py` need a display/Xvfb this run did not have;
  `verify_recovery.py --live` interrupts real audio and is not run unattended.

Unchanged from 0.6.0: no VBAN, ASIO, external FX inserts, surround upmix or
oversampled true-peak limiting; a standalone PulseAudio server is not
supported. ASIO for Windows applications under Wine remains the separate
wineasio driver, not bundled (see [plugin setup](PLUGINS.md)).
