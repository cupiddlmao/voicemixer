Voicemixer 0.6.0 adds macro control: a named, ordered list of control steps that
run against the existing `--ctl` vocabulary, triggered from the command line, a
row of run buttons in the console, or the new macro editor.

> DRAFT — not yet released. The version is not bumped and no archives are built
> until the validation record below is complete and reviewed.

## Changes
- **Macros.** A macro is a named list of steps, each step the same `{verb, target,
  value}` command the `--ctl` queue already accepts (`mute`, `gain`, `bypass`,
  `denoise` over `mic1`–`mic5`, `system`, `music`, `aux`, `a1`–`a5`, `b1`–`b3`).
  Steps are stored under a `macros` key in `settings.json`; no migration is needed
  for existing profiles. A macro applies as one atomic update: the steps run
  against a copy of the settings that replaces the real ones only when every step
  succeeds, and the engine receives a single coalesced `apply()`.
- **Triggering.** `linux-mixer --ctl macro <name>` runs a macro from a terminal or
  desktop keybinding; names may contain spaces. Exit codes are unchanged: 2 for
  invalid input or an unknown macro, 1 when no mixer is running. The client
  validates the name against the saved `settings.json` before queuing, so a macro
  that exists only in the running controller's memory is refused.
- **Run row.** The console status bar gains a `MACRO` section with one Run button
  per defined macro, in the existing button style. Failures are reported in the
  status line, never a dialog. A macro that contains a toggle notes that running
  it twice flips it back.
- **Editor.** Menu → macro editor: a plain dialog with the macro list, editable
  step rows, New/Delete/Rename, Run and Test, and a status line that names the
  first failing step. Edits stay in a draft until Save validates the whole list
  against the other macros and issues one apply and save. Test validates without
  storing.
- **Guardrails.** A macro cannot call another macro (refused by three independent
  checks, so runs cannot nest) and cannot contain a step that would rebuild the
  audio graph — the refusal is by verb class, so any future graph-rebuilding verb
  is refused by rule. Profile bundles validate the `macros` structure on import
  (list size, name and step lengths) and round-trip it.
- **Tests.** `macro-commands` and `macro-editor` ctest cases cover the model,
  clamping, whole-macro rejection, persistence round-trips, the editor dialog
  against a temporary state directory, and the refusal paths. `verify_control`
  covers the queue and exit codes; `verify_profile` covers macros in bundles;
  `tests/verify_macros.py` starts a real controller on an isolated session and
  asserts both steps of a two-step macro land before the first poll after the
  trigger, plus the negative case.

## Downloads
Assets are produced by `python3 packaging/release.py` and uploaded to this release:
`linux-mixer-0.6.0-ubuntu26.04-x86_64.deb`, `linux-mixer-0.6.0-ubuntu26.04-friend.zip`,
`linux-mixer-0.6.0-arch-friend.zip`, `linux-mixer-0.6.0-gentoo-friend.zip` and
`linux-mixer-0.6.0-source.tar.gz`, with `SHA256SUMS`. Verify against `SHA256SUMS`
before installing.

## Keep your settings
Install over the previous version using the same prefix. Quit the controller first
(**Menu → Quit mixer and stop audio**, or `linux-mixer --quit-running`); closing the
window only hides it. Settings, devices, routes, app assignments, recorder options
and plugin racks are retained. Macros are part of `settings.json` and travel with a
profile bundle. See [updating without losing settings](UPDATING.md).

## Validation and limits
VALIDATION_PLACEHOLDER — to be replaced with the recorded results of the full
pre-release pass: ctest suite, the Python integration checks on an isolated
session, and the numbers that fill this section honestly. Clean desktop
installations of 0.6.0 have not been validated.

Unchanged from 0.5.x: no VBAN, ASIO, MIDI, external FX inserts, surround upmix or
oversampled true-peak limiting; a standalone PulseAudio server is not supported.
The macro feature changes no audio processing: it writes existing settings fields
through the existing queue, so it adds no DSP risk by construction, and the
graph-rebuild refusal keeps it from restarting the engine.
