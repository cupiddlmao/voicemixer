# Macro control

> **Added in Voicemixer 0.6.0.** This document describes the macro feature as
> released: the model, validation, execution, the editor dialog, the run buttons
> in the console and the `--ctl macro` trigger, tested as described below.

This document follows the plan in [the 0.6.0 feature plan](PLAN-0.6.0.md), sections 4
and Phase 6, and describes what the code on `main` actually does. Where something is
unverified it says so rather than promising it.

## What a macro is

A macro is a named, ordered list of steps. Each step is the same
`{verb, target, value}` text the command line already accepts, so a macro is the
existing `--ctl` vocabulary collected under one name. Steps are stored as strings:

```json
{"name": "Stream", "steps": ["gain mic1 -6", "mute music on", "denoise mic1 on"]}
```

under the `macros` key of `settings.json`. A settings file with no `macros` key loads
as "no macros" — no migration is needed for an existing profile.

Running a macro applies every step to the same settings object and then issues a
single `engine.apply()`. That call is coalesced onto a 20 ms timer, so all of the
steps reach the audio graph in one update. From the graph's point of view the macro
is applied at once: there is no audible intermediate state where the microphone is
already pulled back but the music is not yet muted.

That property is the whole point of the feature and is deliberately protected, in two
places. A step is parsed by the same code as the command line, and a macro is
executed against a *copy* of the settings which replaces the real ones only when every
step succeeds. A failure therefore leaves the settings exactly as they were, and the
connecting code issues one `apply()` and one `save()` for the whole run. An
implementation that called `apply()` once per step would be a slow sequence of
keypresses, not a macro, and the header comment in `native/macros.h` says so.

A macro has no state of its own. It is a list of settings changes; it does not
remember whether it was last run, and it does not toggle a flag somewhere to say so.

## Steps and validation

The four verbs a step may use:

- `mute` — `on`, `off` or `toggle`
- `gain` — dB, clamped to `-60..+12` by the shared parser
- `bypass` — `on`, `off` or `toggle`, microphones only
- `denoise` — `on`, `off` or `toggle`, microphones only

Their targets are the command line's target list: `mic1`–`mic5`, `system`, `music`,
`aux`, `a1`–`a5` and `b1`–`b3`.

A macro is validated as a unit. `validateMacro()` requires a non-empty name, at least
one step and a name no other macro already uses (compared case-insensitively), and
then checks every step: the target exists, the verb applies to that target
(`bypass`/`denoise` are mic-only), and the verb is one a macro may run. A macro
containing one bad step is rejected entirely; its valid steps are never applied.

At load time a malformed macro is dropped on its own and the reason recorded, so the
rest of the file still loads. When the controller starts and finds such errors it
shows them in the status line (`Macro settings: …`) instead of failing silently. A
duplicate name is dropped the same way, keeping the first definition.

At run time each step is validated again before it is applied — a step can arrive
hand-built from an editor, and applying one with an out-of-range index would be
unsafe. A run stops at the first failing step and the error names it
(`macro 'Half' step 2: …`), but because of the copy described above nothing from the
earlier steps remains applied. "Stops at step 2" and "changed nothing" are both true.

`toggle` is accepted. See below.

## The `toggle` caveat

Running a macro twice flips twice.

A `toggle` step resolves against whatever the value happens to be when the macro
runs. It is not "turn it on"; it is "change it". So a macro containing
`mute music toggle` will mute music the first time and unmute it the second time, and
the person who bound it to a key is likely to want only the first behaviour.

This is usually not what someone means by a macro. Bind `on` or `off` explicitly
unless you genuinely want a switch. The same applies to a macro whose target is
changed by hand between runs: the macro has no memory, so it cannot know the strip was
muted by someone else.

The editor flags this when it loads or edits a macro: `macroContainsToggle()` walks
the steps on demand, so it cannot go stale after an edit, and it only counts verbs that
actually carry an on/off mode — `gain` keeps the default mode value and is not reported
as a toggle. The console uses the same check after a run and appends
`· contains toggle: running it twice flips it back` to the status line.

## Running a macro from the console

The bottom status row of the console carries a macro section. When at least one macro
is defined, the row gains a small `MACRO` label and one Run button per macro, each
labelled with the macro's name (elided if it does not fit). Hovering a button says
what it does. With no macros defined the row is exactly as it was before.

Clicking a button runs the macro in the window itself: `runMacro()` against the live
settings, then one `apply()` and one `save()`. It does not go through the command
file, so there is no poll delay — the only latency is the 20 ms apply coalescing.

- On success the status line reads `Macro '<name>' applied`, plus the toggle note if
  the macro contains one.
- On failure the status line shows the error itself, in place of the usual status
  text, and nothing is saved. There is no dialog and no partial application.

Two limits are worth stating plainly:

- The macro row is a fixed width strip in a status bar, not a panel. Roughly fifteen
  buttons fit before the row runs into the audio-health readout; macros beyond that
  are simply not drawn, and there is no scrolling or overflow menu. Use `--ctl` or the
  editor for those.
- The status text shares the row, so when macros are present it is elided to about
  500 px. A long failure message can be cut off in the window; the full text is the
  same message the command path reports.

## Triggering a macro from the command line

Macros are also triggered through the existing command-line queue:

```bash
linux-mixer --ctl macro <name>
```

The name is the rest of the line, joined with single spaces, so it may contain spaces
(`--ctl macro Stream Mode` runs `Stream Mode`). Names are matched case-insensitively
and whitespace is normalised.

The client writes a command file and the running controller reads it on its existing
poll, which runs every 700 ms — so the macro takes effect within roughly that long,
which the README rounds to "about a second". The delay is the poll interval, not the
macro: once the command is read, every step lands in one graph update. Binding a macro
to a key does not make it faster or slower than the single-parameter commands the
README already recommends.

Exit codes:

- **0** — parsed (and the name exists in the saved settings); the command was queued.
- **2** — invalid input, or an unknown macro name.
- **1** — no mixer is running, or the command file could not be written.

Two honest caveats about that, both of which follow from where the name is checked:

**The client validates the name against the saved `settings.json`, not against the
controller.** `--ctl macro` reads `settings.json` from the state directory on every
invocation and refuses a name that is not in it. A macro that exists only in the
running controller's memory — one that has been added or renamed without being saved —
is therefore refused with exit code 2 and never queued, even though the window may
still be showing it.

**A hand-edited `settings.json` is accepted by the client immediately, but only takes
effect after the controller reloads.** The client re-reads the file each time, so an
edit made outside the program is enough for the name to be accepted (exit 0, command
queued). The running controller is not re-reading it: it loaded settings at startup
and holds its own copy. Until it reloads — in practice, until it is restarted — it
does not know the macro, the queued command fails with `unknown macro`, the reason
appears in the controller's status line, nothing is applied, and the exit code has
already been returned as 0. Worse, the controller writes `settings.json` from its
in-memory copy when it saves, so a hand edit made while it is running can be
overwritten. Quit the controller, edit, then start it again.

One ordering detail: the unknown-name check happens before the "is a mixer running"
check, so with no mixer running a bad name still returns 2 rather than 1.

A queued command whose macro is missing fails on its own: nothing is applied, the
error is reported, the file is removed and the queue keeps draining. That covers a
stale command file written while the macro existed.

**Global hotkeys remain out of scope.** A background application cannot reliably grab
and receive system-wide key combinations on Wayland, which is the same reason the
README points at `--ctl` plus desktop keybindings instead of a hotkey system. Macros
do not change that; they make the desktop-keybinding route worth using, because one
key can now express a whole situation instead of one parameter.

## What a macro cannot do

**A macro cannot call another macro.** A step naming `macro` is refused with
`a macro cannot call another macro`. Nesting would make the single-apply boundary
unclear, so it is refused rather than supported with a depth rule.

**A macro cannot contain a step that rebuilds the audio graph.** This is enforced by
verb class, not by a list of allowed commands in the validator. Every verb is
classified by `commandScope()` as either `Parameter` (mute, gain, bypass, denoise —
they reach the graph through one coalesced apply) or `GraphRebuild` (anything that
would restart the engine), and a verb nobody classified is treated as `GraphRebuild`
rather than assumed safe. A step of the wrong class is rejected with
`'…' would rebuild the audio graph and cannot run in a macro`.

It is worth being precise about what that guard covers today. The command vocabulary
contains no device, route, mode or sample-rate verbs at all, so there is nothing
qualified as graph-rebuilding to put in a macro yet; the classification exists so that
if such a verb is added later it is refused by default rather than by remembering to
add it to a deny list. What this means in practice is that macros are a parameter
recorder, not a scripting system: they cannot choose or change a device or a hardware
strip's channel, switch a bus between Normal and Composite or edit a patch, change the
sample rate, or edit routes, sends and application rules.

A later version could add route verbs, but route changes can restart audio, so it is
not a small addition — the atomicity claim would have to be weakened or the verb
restricted. That decision is not made here, and this document does not promise it.

There are also no ramps and no delays. A step is a value, not a move over time. The
mixer's existing smoothing is a 10 ms per-parameter fade, not a time-parameterised
transition, so a "fade mic1 to −12 dB over two seconds" step would need either
DSP-level targets or a controller-side timer issuing many small commands — and the
latter fights the apply coalescing and steps audibly. Macros do not do it.

There are no conditionals, no state variables and no loops.

## Not synchronised to anything

Nothing here is synchronised to tempo or to audio position.

A macro is not a musical event. It does not read a clock, a MIDI timecode, a transport
position, or the position of anything playing. It has no quantisation, no bar or beat
alignment, and no notion of where the audio currently is. A macro triggered at 3.7
seconds into a track runs at 3.7 seconds, whenever the trigger arrived — up to a poll
interval late, and never nudged onto a grid to hide that.

If a macro is triggered by a key press while audio is playing, the only relationship
between the two is that a person pressed the key at roughly that moment.

## Profiles and import

`settings.json` is also part of the complete profile bundle, so importing a profile
carries macros with it. Because the settings loader tolerates malformed entries —
dropping one macro and keeping the rest — the bundle reader validates the *shape* of
the `macros` key before handing anything to it, so an imported profile cannot smuggle
a bad structure into the loader:

- `macros` must be an array of at most 256 entries;
- each entry must be an object with exactly two keys, `name` and `steps`;
- `name` must be a non-empty string (after trimming), at most 256 characters, with no
  NUL characters;
- `steps` must be a non-empty array of at most 256 entries, each a non-empty string of
  at most 256 characters with no NUL characters.

Anything else makes the profile fail validation as "Malformed mixer settings: invalid
field, type or array size", and nothing is imported.

This is a shape check, not a semantic one: the step text is not parsed at import.
A profile can therefore pass with a step like `explode mic1` and import "successfully",
after which `MacroSettings::load` drops that single macro and records the reason, which
the controller shows in the status line. The import is refused for structure, and
per-macro validity is decided afterwards by the same validation every other path uses.

## The editor

Implemented as `native/macro_ui.cpp`, opened from the console **Menu**. It is a plain
dialog in the `profile_ui.cpp`/`recorder_ui.cpp` style: the macro list on the left,
editable step rows on the right (each step shown as its canonical `--ctl` text),
New/Delete macro, Add/Remove step, Rename, Run and Test buttons, and one status line
that names the first failing step after a run or a validation failure. A macro that
contains a toggle shows the run-twice warning once in the editor.

Edits accumulate in a draft list and nothing reaches the engine settings until **Save**
validates the whole draft with `validateMacro()` against the other macros — duplicate
names and bad steps are refused with the specific error — and then issues exactly one
`engine.apply()` and `engine.save()`. **Test** only validates the current draft and
changes nothing; **Run** applies the selected saved macro through `runMacro()` and
saves. A new macro is seeded with one default step so it is valid immediately, since a
macro with no steps cannot pass validation. Deleting or renaming a macro that a queued
`--ctl` command still names is not compensated for: the drain path already reports the
unknown macro when that command runs.

## Verification and scope

What the repository actually checks today:

**`macro-commands` ctest** (`tests/verify_macros.cpp`, registered in `CMakeLists.txt`
with `--macro-test`, offscreen, 30 s) covers the model and validation:

- a well-formed macro keeps its name and step order, and every step parses into the
  shared `ControlCommand` form;
- `Settings::json()` → `load()` round-trips macros unchanged, a settings object with no
  `macros` key loads cleanly, and so does an empty list;
- out-of-range gain is clamped to `+12` and `-60`;
- an unknown target, an unknown verb and an empty step list each reject the macro;
- a duplicate name is dropped keeping the first definition, and one malformed entry is
  dropped while its well-formed neighbours survive;
- one invalid step rejects the whole macro, with the valid steps demonstrably
  unapplied;
- validation also refuses an empty name, a macro with no steps and a duplicate name;
- `toggle` is accepted and reported by `macroContainsToggle()`, an explicit `on` is
  not, and a `gain` step is not mistaken for one;
- a successful run lands all four kinds of step on the one settings object;
- a failing second step leaves the settings byte-identical to before, and the error
  names step 2;
- a toggle step runs and a second run flips it back.

**`control-commands` ctest** (`tests/verify_control.cpp`) covers the command surface:

- `macro <name>` parses, including a name with spaces; `macro` alone and a blank name
  are rejected;
- with a macro saved in an isolated state directory, `--ctl macro Stream` returns 0 and
  queues, and an unknown name returns 2 without queuing;
- draining that queue runs the macro through `runMacro()`, applies both steps and
  counts the whole thing as one command, leaving the queue empty;
- a stale hand-written `macro Missing` file fails alone: nothing is applied, one error
  is reported, and the queue is still emptied.

**`complete-profiles` ctest** (`tests/verify_profile.cpp`) covers import: three
malformed `macros` shapes are rejected, and a profile carrying a valid macro validates,
survives the bundle round-trip, reaches `Settings` intact and is preserved when the
profile is installed.

**`macro-editor` ctest** (also `tests/verify_macros.cpp`, `--macro-editor-test`, its
own case in `CMakeLists.txt`) covers the dialog headlessly, running its own `Engine`
against a temporary state directory so it never touches a real `settings.json`:
creating a macro with two steps and saving carries it into the engine settings with a
single apply cycle; a duplicate name and a bad step each leave the saved settings
untouched and the status line names the first failing step; deleting removes the macro;
Run applies the selected macro; Test only validates.

Three things are not covered and should be reported as untested rather than as
working:

- **The console run row and its status messages** have no test. The button layout, the
  elided status text and the toggle note are exercised only by looking at the window.
- **The refusal paths** — a nested macro and a graph-rebuilding verb — are implemented
  and give specific errors, but no test asserts them. The nested case is refused by two
  independent checks; the graph-rebuild case cannot be tested with today's vocabulary,
  because no such verb exists.
- **End to end.** The integration script the plan called for (`verify_macros.py`,
  starting a controller against a real engine and reading values back) does not exist.
  So "every step lands in one graph update" is argued from the code — one `runMacro()`,
  one `apply()` — and from the tests above, which assert a single settings object and a
  single call, but nothing measures apply cycles against a running engine. Also
  untested: a burst of macro runs inside one 700 ms poll window, real keyboard or
  controller triggering, and the audible result of a macro that moves several faders at
  once, which is a listening judgement rather than something a synthetic graph can
  measure.

This document describes `main` at the time of writing. It does not change the version
and does not commit the project to shipping macros with any particular release.
