# MIDI control

> **Added on the development branch, planned for Voicemixer 0.7.0.** This document
> describes MIDI control as it stands on `main`: the binding model, the ALSA
> sequencer reader, the message mapping, the bindings editor with learn mode and the
> console wiring, tested as described below. The version number is not bumped yet.

This document follows the plan in [the 0.6.0 feature plan](PLAN-0.6.0.md), section 3,
and describes what the code on `main` actually does. Where something is unverified it
says so rather than promising it.

## What a binding is

A binding is one MIDI message mapped to one action. The message is a control change or
a note-on; the action is the same `{verb, target, value}` text the command line already
accepts, or the name of a saved macro. Bindings are stored as a list under the `midi`
key of `settings.json`, beside `macros`:

```json
{"midi": [
  {"id": "mic-fader", "message": "cc", "channel": 0, "number": 74, "action": "gain mic1 -6"},
  {"id": "music-mute", "message": "cc", "channel": 0, "number": 64, "action": "mute music"},
  {"id": "stream-start", "message": "noteon", "channel": -1, "number": 60, "action": "macro Stream"}
]}
```

The five keys of an entry:

- `id` — how a binding is addressed by the editor. Non-empty and unique, compared
  case-insensitively.
- `message` — `"cc"` or `"noteon"`. No other word is accepted; unknown text is an
  error, never a guess.
- `channel` — `0`–`15`, or `-1` for any channel.
- `number` — the controller or note number, `0`–`127`.
- `action` — canonical `--ctl` text: `mute`, `gain`, `bypass` or `denoise` with one of
  the command line's targets, or `macro <name>`.

The action text is the CLI's own spelling, not a second dialect. It goes through
`parseControlCommand()`, the parser the `--ctl` queue and macro steps already use, so
the gain clamp and the target names are shared. `"macro Stream"` stores the macro verb
and the name; unlike a macro, which may not call another macro, a binding may name one.

One spelling detail follows from that: an action with the default `toggle` mode is
written without the word, as a macro step is, so `"mute music"` in the file is a
toggle. Typing `mute music toggle` in the editor is accepted and stored in that same
canonical form, so a hand edit and a saved edit produce the same file.

A settings file with no `midi` key loads as "no bindings", and an explicit empty list
does the same. `json()` writes the key only when at least one binding exists, so saving a
file that has none leaves no MIDI key behind. The same convention covers the reader
switch: `midiEnabled` is written only when it is `true`, and an absent key means off.

At load time a malformed entry is dropped on its own: a missing id, an unknown
`message` word, a channel or number outside its range, a missing channel or number, or
an action that does not parse. The rest of the file still loads, and the reason is
recorded in `MidiBindings::errors`. **Nothing currently displays that list.** Macro
load errors reach the status line as `Macro settings: …`; the MIDI equivalent does not
exist yet, so a binding dropped at load shows up as a row that is simply not in the
editor. That is a gap in the wiring, not in the model.

Channel and number are required rather than defaulted: a hand-edited entry missing one
would otherwise become a binding on channel 0, controller 0 — a mapping nobody wrote,
which either does nothing or fires on a controller number nobody chose.

An action that was never set is refused instead of defaulted. The empty `ControlCommand`
happens to spell a real command (`mute mic1 toggle`), so a binding without an action is
an error rather than an accidental mute.

Duplicate ids are the one refusal that is not per entry. The list is how a binding is
addressed, so a file that repeats an id is not trusted: the collection loads empty and
the message names the id, rather than keeping whichever entry came first. Parsing and
validation are also split on purpose. Load decides whether an entry is well-formed
text; validation decides whether the thing that text names exists. A binding naming a
macro that has since been deleted therefore still loads, stays visible in the editor,
and is refused when saved — it does not disappear from the file silently.

## How a message becomes an action

The rules live in one pure function over the bindings and one message, so the reader and
the tests share one definition of what a value means.

- **A CC bound to `gain`** ignores the stored dB and maps the whole value range:
  `db = -60 + (v / 127) × 72`. `v = 0` is −60 dB, `v = 127` is +12 dB, about 0.57 dB
  per step, and every position moves the fader. The stored value is a default, not a
  target.
- **A CC bound to `mute`, `bypass` or `denoise`** keeps the stored verb and target and
  ignores the stored mode: `v ≥ 64` is on, `v < 64` is off. A binding saved as
  `mute music on` still follows the wire, which is what makes a knob usable as a switch
  without saving one mode or the other.
- **A note-on with velocity above zero** runs the stored action exactly as parsed, mode
  included. `toggle` behaves as a button press: one press flips it, the next flips it
  back (see [the toggle caveat](MACROS.md#the-toggle-caveat)). A note-on bound to `gain`
  keeps its stored dB — velocity is not a fader position.
- **Velocity 0** is the wire's note-off and runs no action. Nothing is queued or
  applied, so a pad that reports release as velocity 0 does not fire twice. Learn still
  sees the message, because it watches the raw stream rather than the matches.
- **A CC bound to `macro`** triggers at `v ≥ 64` and does nothing below it.
- **The channel** matches any channel when the binding says `-1`, and only its own
  channel otherwise.

Several bindings may answer one message. One CC moving a fader and starting a macro is
a normal setup, not an ambiguity: every matching binding fires, in the order the
bindings are stored in the file. A message that matches nothing is the common case —
a device transmits every message past every subscriber — and is not an error: nothing
is queued and no action hook is called. A caller handing in a class, channel, number or
value outside the wire ranges gets nothing back rather than a clamp: a value of 200
mapped to "full gain" would look like a real press.

Only control change and note-on are understood. Pitch bend, aftertouch, program change,
clock and the sequencer's own explicit note-off event type are dropped at the reader: the
model has no place to put them, and inventing a mapping inside the event handler would be
the wrong layer.

## What does not exist yet

Partial-range gain maps, relative encoders, 14-bit CC pairs, response curves and ramps
are not implemented. The gain mapping is the whole `-60..+12` range per binding, a
binding cannot say "only use the middle of the fader", and a knob that sends relative
increments (a common encoder mode) cannot be bound — it arrives as CC values and would
be read as an absolute position. A 14-bit pair would need both controllers tracked as
one binding.

The stored shape was left able to grow into these — an entry can gain a key without an
older build refusing it, because unknown keys inside a binding are ignored and that is
tested — but nothing in this build reads such a key.

## The reader

The input side is the ALSA sequencer through `libasound`. The reader opens its own
sequencer client named **Linux Mixer** with one input port named **Linux Mixer MIDI**;
other clients send to that port. Both names are part of the contract, so a controller
can be connected once in a patchbay such as `aconnect` and survive a restart of the
mixer.

Discovery is by port list, not by device node. The reader walks the sequencer's clients
and ports, subscribes to every port that can be read from and subscribed to, and
rebuilds that set on a client-start, client-exit, port-start or port-exit event. That
is what gives hotplug without polling `/dev/snd/midiC*D*`: a controller plugged in after
the mixer starts is picked up by the next ports-changed event. The mixer's own client
and the sequencer's system client are skipped, and a subscription the sequencer refuses
is simply retried on the next such event.

Because the reader subscribes to every readable source itself, a controller usually
works without being wired up at all. An explicit connection also works; a second,
duplicate subscription is refused by the sequencer and does no harm.

`libasound2-dev` (`alsa-lib` on Arch) is already in the README's build requirements, so
this adds no package, and CMake refuses to configure with a clear message when the
library is missing instead of failing later at link time.

**A system without an ALSA sequencer cannot use MIDI control.** The sequencer is
`/dev/snd/seq`, which a container, a CI runner or a kernel built without `snd-seq` may
not have. The mixer still builds and runs there; only MIDI is dead. When the reader
cannot open a client, the console puts the reason on its status line —
`MIDI input not started: cannot open the ALSA sequencer: …` — rather than opening a
dialog, because this happens at start-up and there is nothing to decide. Ticking
**MIDI enabled** in the editor with a broken sequencer says `MIDI not started: …` in
the editor's own status line and keeps the flag saved, so a sequencer that appears later
is picked up on the next start rather than the choice being silently thrown away.

## One update per batch

Events arrive on the reader's own thread, never on the GUI thread. That thread owns the
sequencer handle and a locked copy of the bindings, maps one received message onto the
actions of every binding that matched, and hands the batch over. The console's handler
does one thing with it: it marshals a queued call and returns. Nothing touches
`Settings`, the engine or a widget from the reader thread, which is the rule the meter
readers follow as well.

On the GUI thread the whole batch is applied to the same settings object and committed
with a single `engine.apply()` and a single `engine.save()`. That is the same one-apply
discipline a macro run keeps, for the same reason: the graph must not be poked once per
action, and the console must never paint an intermediate state. Every event produces one
batch — the actions of every binding that matched that one message — and the engine's
apply is coalesced onto a 20 ms timer, so a fast fader sweep collapses into successive
updates instead of one per byte.

A batch whose actions were all skipped issues no apply at all. An action naming a macro
that no longer exists is skipped and named on the status line
(`MIDI 'stream-start': no macro named 'Retired'`); it is never a dialog, because this
runs while the user is turning a knob and the console may not even be visible. A macro
action is resolved by name at the moment it fires and runs through `runMacro()`, so a
macro edited since the binding was saved runs its current steps, and a failing macro
leaves its own settings untouched like any other macro run.

## Turning it on

MIDI is off by default. The switch is a **MIDI enabled** checkbox in the bindings
editor, opened from the console **Menu → MIDI bindings…**, and it is live rather than
draft state: ticking it opens the reader, unticking it closes the reader, and the flag is
written to `settings.json` immediately so a crash before **Save** cannot leave the file
saying the opposite of what the reader is doing. The bindings themselves are only
written by **Save**, so **Close** discards row edits and keeps the switch.

The console owns the one reader. It starts it at start-up when `midiEnabled` is on,
refreshes the bindings after the editor closes and after a profile import, and stops it
on the real quit path. Hiding the console to the background keeps the reader running,
like the rest of the controller.

Bindings persist in `settings.json` and travel with a complete profile bundle. The bundle
reader validates their shape before handing them to the loader — `midi` must be an array
of at most 256 objects, each with exactly the five keys above, `message` exactly `"cc"` or
`"noteon"`, `channel` an integer in `-1..15`, `number` an integer in `0..127`, and
`midiEnabled` a boolean — so an imported profile cannot smuggle a bad structure in. As
with macros this is a shape check, not a semantic one: an action naming a macro the
profile does not contain passes import and is refused later, by the editor, with the
macro's name.

## The editor

Implemented as `native/midi_ui.cpp`, opened from the console **Menu**. It is a plain
dialog in the `profile_ui.cpp`/`recorder_ui.cpp` style: a table with one row per binding
and five columns — binding id, message, channel, controller or note number, action —
with New, Delete and Learn below it, the **MIDI enabled** switch, and one status line
under everything.

Channel is spelled `any` for `-1`, so the sentinel does not have to be remembered. A
message is typed as `cc` or `noteon`; an action as the `--ctl` text, including
`macro <name>`. A hint line above the switch says all of that, because the table itself
has no tooltips.

**New** appends a row that is already valid and harmless — id `binding`, `cc`, `any`,
number 0, `mute mic1 on` — with an id that no existing row uses, so only the message the
user cares about needs changing. **Delete** removes the selected row. Edits live in the
table and nowhere else.

**Save** parses every row, then validates the whole list with `validateMidiBindings()`
against the saved macros. The refusals are the model's own: an unknown message word, a
channel that is not `any` or `0`–`15`, a number that is not a whole number, an action
that does not parse, a duplicate id, a macro name that does not exist. A refusal puts
`Not saved: …` and the specific reason on the status line and leaves `engine.settings`
and `settings.json` exactly as they were — no dialog, exactly like the macro editor. On
success the rows are rewritten in canonical form (the text the next load and the reader
will see), the loaded-list error breadcrumbs are cleared because the stored list has just
been validated, and the editor issues exactly one `engine.apply()` and one
`engine.save()`. **Close** discards row edits and leaves the saved bindings alone.

## Learn mode

Select a row, press **Learn**, then move the knob or press the key. The next message the
reader receives fills that row's message, channel and number. The action text is left
alone — Learn learns the message, not what it should do.

Learn listens to the raw message stream rather than to matched bindings, which is the
point: the message being bound is by definition one that no binding matches yet. Every
CC and note-on the reader understands is offered to the hook, matched or not. Learned
numbers live in the row until **Save**, like any other edit; the editor shows them in
canonical form after saving.

With MIDI disabled there is nothing to learn from, and the editor says so instead of
waiting: `MIDI input is not running; tick "MIDI enabled" to learn a message.` The hook is
armed only while Learn is waiting for a message and is cleared when a message arrives,
when the editor closes and when the reader stops, so it can never outlive the widget
that armed it.

## What this feature is not

**It is not a hotkey system, and it does not add global hotkeys on Wayland.** A
background application cannot reliably grab and receive system-wide key combinations on
Wayland, which is why the README points at `--ctl` plus desktop keybindings — suitable
for KDE, GNOME and Hyprland keybindings — instead of a hotkey grab. MIDI control does not
need a grab at all: the message arrives through the ALSA sequencer, which is exactly why
it serves the people who wanted a physical control surface on Wayland and could not have
one.

**It is not synchronised to tempo or transport.** Nothing here reads a clock, MIDI
timecode, a transport position or the position of anything playing. There is no
quantisation and no beat alignment: a controller message runs when it arrives.

**It is not a second control vocabulary.** A binding runs the same `--ctl` commands
through the same parser, so the gain clamp (`-60..+12`), the target list
(`mic1`–`mic5`, `system`, `music`, `aux`, `a1`–`a5`, `b1`–`b3`) and the verb rules
(`bypass` and `denoise` are mic-only) are the shared ones, not restated. A verb that
would rebuild the audio graph is refused, the same classification macros use, so a
binding cannot choose a device or change a sample rate. A binding may call a macro, and a
macro may not call another macro, so nesting is one level deep by the existing rule.

**It has no device-specific profiles.** A binding records a message class, a channel and
a number — no device name, no model, no port id. Two controllers sending CC 74 on
channel 0 therefore both move the same fader, and moving a device to another USB port
changes nothing. The flip side is that per-device layouts are just conventions in the id
you choose (`nanokontrol-fader-1`), not something the mixer enforces or can switch
between.

**There is no port selection either.** The reader listens to every readable source port
on the sequencer and cannot be told to ignore one, so a second application sending the
same controller numbers on the same channel will fire the same bindings.

## Verification and scope

What the repository actually checks today, all offscreen with `QT_QPA_PLATFORM=offscreen`:

**`midi-bindings` ctest** (`tests/verify_midi.cpp`) covers the model:
a stored binding keeps its id, message, channel, number and action, and that action is
the shared `ControlCommand`; writing and reloading round-trips the list and leaves the
other settings keys intact; the stored text is the CLI's own spelling with `toggle`
dropped; unknown keys inside an entry are ignored; an absent `midi` key, an empty list
and a non-list value each load without crashing or inventing bindings; one malformed
entry is dropped while its neighbours survive; a duplicate id loads the list empty; a
binding naming a deleted macro loads but is refused by validation, and is accepted
again once the macro is saved; channels and numbers are held to `-1..15` and `0..127`,
edges included; unknown verbs, unknown targets, `bypass`/`denoise` on a non-mic, a
macro action with no name, and an unset action are all refused; `"cc"`, `"noteon"` and
nothing else are accepted as message classes.

**`midi-input` ctest** (`tests/verify_midi_input.cpp`) covers the mapping through the pure
functions, with no device: all 128 fader positions land inside the clamp and never move
backwards; a switch flips either side of 64 and a stored mode is ignored; a note-on runs
its stored mode, velocity 0 delivers nothing and a note-on fader keeps its stored dB;
`-1` matches any channel while a pinned channel rejects the others; a macro fires at 64
and not at 63; two bindings on one message both fire in stored order; an unbound number,
an empty list and a mismatched message class all deliver nothing; a binding without an
action cannot fire; out-of-range wire values are refused. Then, when `/dev/snd/seq`
exists, it goes through the sequencer for real: a second client opened inside the same
test process subscribes to the reader's own client and port and sends CCs and note-ons
through ALSA, and the reader delivers the bottom and top of a fader, a switch at 63 and
64, a note-on on channel 5, nothing for velocity 0 or the wrong channel and something for
the right one, and a macro at 63 and 64. The final line says which of the two paths the
run took, so a machine without a sequencer reports a skip rather than a pass.

**`midi-editor` ctest** (`tests/verify_midi_editor.cpp`) covers the dialog headlessly,
against a temporary state directory so a real `settings.json` is never touched: two rows
typed in and saved with one apply cycle; the persisted JSON is the canonical shape, has no
`midiEnabled` key while the switch is off, and reloads into a fresh `Settings`; a binding
naming a missing macro and a channel of 17 are each refused with the model's wording,
leaving the stored file byte-identical; the switch writes `midiEnabled`, opens the real
reader and closes it again; Learn is filled from a sequencer message and only stored by
Save; with the reader stopped, Learn says so. It then drives `applyMidiBatch()`, the
function the console's queued slot calls, rather than instantiating `Console`: one
notification for a two-action batch (a fader at the top of its range plus a music mute),
one for a macro resolved by name, and one for a missing-macro batch that changed nothing,
with the status line naming it.

What is not covered should be read as untested rather than as working:

- **No physical controller was available.** The round trip above is in-process: a
  second sequencer client inside the test process, not a device. It proves the software
  path end to end through ALSA — a real client, a real port, real events — but not the
  feel of real hardware: no test says a nanoKONTROL's faders land where the user
  expects, how a device's buttons report press and release, or whether a particular
  controller sends the channel and numbers its manual claims. The 0.6.0 plan said the
  same thing before any of this was written.
- **The console wiring has no test.** The editor test calls `applyMidiBatch()` directly,
  without a `Console`, a tray icon or a PipeWire session, so "the reader starts at
  start-up when enabled", "the reader survives hiding to the background" and "the status
  line shows the failed open" are argued from the code, not measured.
- **Hotplug is untested.** The test connects its sender explicitly, so the automatic
  subscription to a port that appears after start-up — the ports-changed rebuild — has no
  assertion behind it.
- **A reader that fails after it opened goes quiet.** Failures while receiving are kept in
  the reader (`takeError()`), but nothing in the window polls that, so a sequencer that
  stops mid-session leaves the status line showing whatever it showed before. The same
  applies to the reader's polling queue (`takeActions()`), which a caller may drain; the
  console uses the immediate hook instead.
- **The audible result** of a fader move, a fast sweep or several bindings firing at once
  is a listening judgement, not something a synthetic graph measures.

This document describes `main` at the time of writing and does not change the version.
The 0.7.0 above is a plan, not a promise.
