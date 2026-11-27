# Voicemixer 0.6.0 feature plan

This document is a cost estimate, not a schedule. Nothing in it is implemented, and
nothing in it is promised for a particular release. Its purpose is to make the price
of each candidate feature visible — in code, in risk and in what can actually be
tested here — before anyone commits to one.

The release is called 0.6.0 only because that is the next minor number after 0.5.1.
Writing this file does not change the version, and no version is bumped until a
feature is finished and validated.

## What exists today

Grounding, so the estimates below are not guesses:

- The Qt controller (`native/`) never processes audio. It writes a PipeWire
  configuration and manages the graph around it.
- `matrixConfiguration()` in `native/routing_settings.cpp` generates
  `.runtime/matrix.conf`, which `RoutingMatrix::start()` launches as a separate
  `pipewire -c` process. Everything audible is described there: three virtual input
  filter chains, eight bus filter chains, and the LADSPA nodes loaded from
  `libmixer_dsp.so`.
- The graph shape is fixed at build time. The bus capture nodes take 18 channels
  (`AUX0`–`AUX17`), the mixers expose eight gain ports, and `RoutingSettings::routes`
  is an 8×8 array. There is no dynamic input count.
- Live parameter changes go through `RoutingMatrix::apply()`, which pushes values with
  `pw-cli set-param`; changes that alter the node set instead restart the audio graph.
  `consume()` rejects a graph whose `linux.mixer.matrix.version` marker is not `4`, and
  `start()` refuses to build over an old graph, so any graph change forces one engine
  restart and shows the user "Old audio graph detected".
- Per-strip plugins are already an isolated-process insert. `synchronizeLinks()`
  builds an `inserts[]` array and swaps the dry edge for `pluginNodeName(input)` only
  when the host is running with both `FL` and `FR` ports present, falling back to dry
  otherwise. That pattern is the template for several items below.
- Remote control is a file queue. `linux-mixer --ctl` writes a command file,
  `drainControlCommands()` reads it on a 700 ms poll in `native/main.cpp`, applies it
  with `applyControlCommand(Settings&, ...)`, then calls `engine.apply()` and
  `engine.save()`. The vocabulary today is `mute`, `gain`, `bypass` and `denoise`
  over `mic1`–`mic5`, `system`, `music`, `aux`, `a1`–`a5`, `b1`–`b3`.
- `engine.apply()` is coalesced onto a 20 ms timer, so several parameter changes made
  in one controller turn reach the graph in one update.
- The ctest suite is nine cases: `control-commands`, `complete-profiles`, `dsp`,
  `meter-reader-thread`, `recorder-capture-thread`, `recorder-history`,
  `recorder-links`, `owned-process-ledger`, `stability-and-updates`. Integration
  checks are Python scripts driving a real engine with synthetic graphs
  (`verify_matrix.py`, `verify_hardware.py`, `verify_eq_fx.py`, `verify_plugins.py`,
  `verify_position.py`, `verify_meters.py`, `verify_recovery.py`).
- The validation record covers Ubuntu 26.04 and CachyOS, PipeWire 1.6.x, Qt 6.10, one
  Razer Seiren Mini and virtual interfaces. There is no multichannel output device,
  no MIDI controller and no second machine on a LAN.

That last paragraph is the single biggest constraint in this document. Several of the
seven items are limited less by code than by hardware that is not present to test
them with.

---

## 1. VBAN

**What it would take.** VBAN is VB-Audio's LAN audio protocol: PCM frames in UDP
packets on port 6980, with `AUDIO`, `TEXT`, `SERVICE` and `SERIAL` sub-protocols, a
handful of packet sizes and a documented range of sample rates. There is no network
code anywhere in this project today; the controller only speaks to PipeWire through
`pw-cli`, `pw-link`, `pactl` and `pw-record`.

Two halves are needed:

- *Sender*: a capture stream on a bus output or a dedicated tap, a packetizer, and a
  socket. Straightforward, and it fits the existing pattern of running small helper
  processes rather than linking new libraries.
- *Receiver*: socket → jitter buffer → sample-rate adaptation → a PipeWire source node
  that behaves like an input. This is the expensive half, because the input side of
  the matrix is fixed. Either the received stream is presented as a selectable
  "device" on one of the five hardware strips — which works with the existing device
  and channel selection — or a ninth input strip is added, which changes the 8×8
  routes array, the `Gain 1..8` mixer controls, the composite patch table, the
  settings schema and the `--ctl` target list. The first option is cheap and slightly
  dishonest (a "hardware" strip fed by the network); the second is correct and touches
  a lot of code.

Clock drift is the part that does not go away. Two machines' crystals differ, so a
fixed buffer will slowly overrun or underrun. That needs adaptive resampling or
periodic drop/insert, which is real DSP work with audible failure modes.

**Risk.** High. Real-time networking, a protocol implementation that must interoperate
with software we cannot run here, and drift behaviour that only shows up over tens of
minutes. The `AUDIO` sub-protocol is not authenticated in its publicly documented
form, so a receiver is an open port that injects audio into a live mixer; that needs
an explicit enable, a bind address, and a plain statement in the documentation that
this is trusted-LAN only.

**User value.** Genuinely high for one specific and common setup: a gaming machine and
a streaming machine on the same network, which is exactly who Voicemeeter's VBAN
feature exists for. Low for everyone else.

**Verdict.** The largest item here and the least testable in this environment. Worth
doing eventually, but not first: it would be the only feature in 0.6.0 whose core
behaviour could not be verified before release.

---

## 2. ASIO

**What it would take.** Nothing, because ASIO does not exist on Linux. It is a Windows
driver model; a Linux application cannot present an ASIO device to anything. The
README's phrasing ("ASIO … outside this build") implies a gap that could be closed by
writing code, and that implication is wrong.

The only meaningful reading is: Windows audio applications running under Wine want an
ASIO driver. That is `wineasio` — a third-party Wine driver that exposes ASIO to the
Windows application and talks to JACK/PipeWire on the Linux side. We do not own it, we
would not ship it, and the user installs it themselves, exactly like yabridge in
`docs/PLUGINS.md`.

So the honest work item is a documentation one: a short note in `docs/PLUGINS.md`
saying that Windows DAWs under Wine can reach the mixer through wineasio, that it is
not bundled, and that its latency behaviour is not something this project controls.
There is no change to `native/` and no change to the generated graph. The mixer already
loads `libpipewire-module-rt` with `ifexists`/`nofail` and deliberately does not change
the session quantum.

**Risk.** If treated as a feature: high, because it would be a promise about a driver
we do not write. If treated as documentation: low, with a residual risk that people
read "ASIO supported" and expect a checkbox.

**User value.** Near zero for native Linux users. Some for the Wine-DAW minority, who
mostly already know about wineasio.

**Verdict.** Not a 0.6.0 feature. Recommend removing "ASIO" from the list of things
"outside this build" in the README and release notes, or rewording it to "ASIO for
Windows applications via wineasio, not bundled". Leaving it in a feature plan would be
the one place in this document that overstates what the project can deliver.

---

## 3. MIDI

**What it would take.** Control only. No graph change, no new DSP, no version bump.

The input side is the only open question. Three options:

1. ALSA sequencer via `libasound`. `libasound2-dev` / `alsa-lib` are already listed as
   build requirements in the README, but `CMakeLists.txt` does not link against them,
   so this is still a new link dependency. It brings hotplug and port naming for free.
2. Raw `/dev/snd/midiC*D*`. No dependency at all, but no hotplug, exclusive access, and
   raw byte parsing.
3. A PipeWire MIDI stream through a helper process, in the same style as the meter
   readers and the recorder, which already shell out to `pw-record`/`pw-cat` rather
   than linking PipeWire into the controller.

Option 3 is the most consistent with how this codebase already talks to the audio
server, and it avoids a new build dependency; option 1 is the least code. That choice
should be made before any implementation starts, not during it.

The mapping model is the real design work: note-on and CC bindings, absolute versus
relative encoders, 14-bit CC pairs, a learn mode, per-device profiles, and persistence
in `settings.json` beside `appRules`. Application reuses `applyControlCommand(Settings&,
...)`, so a fader move lands in the same 20 ms coalescing window as a UI change and
needs no new audio path.

**Risk.** Medium-low. The known hazards are: PipeWire/ALSA port names that change
between sessions and machines; MIDI callbacks arriving off the GUI thread, which must
be queued rather than allowed to touch `Settings` directly; and the absence of any
controller here to test with. A virtual ALSA port (`snd-virmidi`) can exercise the
software path end to end, but not the feel of a real fader, which is the entire point
of the feature.

**User value.** High for the streaming and podcast audience this mixer is aimed at. A
nanoKONTROL or X-Touch Mini with real faders is a better experience than the desktop
keybindings the README currently recommends for the same job.

**Verdict.** The strongest competitor to the recommendation below, and the natural
second feature. It is held back only by the dependency decision and by having no
hardware to validate against.

---

## 4. Macro control

**What it would take.** Control only, and the substrate is already there.

A macro is a named, ordered list of steps, each step being the same
`{verb, target, value}` triple the CLI already accepts. Execution is:
`runMacro()` applies every step to the same `Settings` object through
`applyControlCommand()`, then `engine.apply()` is called once. Because `apply()` is
coalesced onto a 20 ms timer, all steps reach the graph in a single update — from the
graph's point of view the macro is atomic, and there is no audible intermediate state
where the mic is boosted but the music is not yet muted. That property is worth
protecting deliberately; it is the difference between a macro and a slow sequence of
keypresses.

Scope for a first version should be deliberately narrow:

- Steps are `mute`, `gain`, `bypass`, `denoise` — the four existing verbs. Nothing that
  requires a graph rebuild (output device selection, Normal/Composite mode, sample
  rate) may appear in a macro, because those restart audio and would break the atomic
  claim above.
- No ramps and no delays. The current smoothing is a 10 ms per-parameter fade, not a
  time-parameterised move; a "fade mic1 from 0 to −12 over two seconds" step would need
  either a DSP-level target or a controller-side timer issuing many commands, and the
  latter fights the apply coalescing and will step audibly. Leave it out and say so.
- No conditionals, no state variables, no loops.

Validation happens per macro, not per step: a macro containing one invalid target is
rejected as a whole at load and at edit, never partially applied. `toggle` is accepted
but flagged in the editor, because a macro run twice flips twice and that is usually
not what someone means.

Triggers are the honest part. There are no global hotkeys on Wayland, which is exactly
why the README points at `--ctl` plus desktop keybindings. So: `--ctl macro <name>`
extends the existing command parser and goes through the same on-disk queue (accepting
its ~700 ms poll), plus a small panel with one button per macro for in-window use. No
global-hotkey promise.

**Risk.** Low, and contained. The worst outcome is a wrong parameter value: visible,
reversible, and confined to settings the UI already exposes. The real risks are
semantic rather than technical — partial application (addressed by whole-macro
validation) and `toggle` re-entry (addressed by documentation and a warning).

**User value.** High relative to the cost. "Mute music, pull mic1 back 3 dB, enable
denoise" bound to one key is the actual daily workflow, and it multiplies the existing
CLI instead of adding a parallel system beside it.

**Verdict.** Recommended. See below.

---

## 5. External FX inserts

**What it would take.** A per-strip send/return pair of virtual nodes: an insert send
(`Audio/Source`) carrying the post-processing signal, and an insert return
(`Audio/Sink`) whose output replaces the direct edge into the matrix. The user patches
an external application — or hardware, through spare interface I/O — between them.

The link-swapping machinery already exists. `synchronizeLinks()` computes `inserts[]`,
checks that both sides of the isolated plugin host are present, and removes the old
edge before adding the replacement. External inserts are the same shape with different
node names, plus a new enable flag per strip.

The costs are elsewhere:

- *Graph version.* Two more filter chains per enabled strip, and a bump of
  `linux.mixer.matrix.version`. That bump forces one engine restart on upgrade and
  shows the existing "Old audio graph detected" message, which is acceptable but needs
  to be in the release notes.
- *Latency.* The plugin host reports its latency to PipeWire today. An external
  application's latency cannot be known. Either the insert is uncompensated and the
  documentation says so plainly, or the user types an offset in samples, which is a
  number most people will not know.
- *Feedback.* Nothing prevents the external chain from monitoring a mixer output while
  feeding the return. The peak limiter is the only protection, and it is a sample-peak
  limiter.
- *Silence.* Nothing guarantees the user's external chain is connected or passing
  audio. The strip must fall back to dry and say so, with the same wording the plugin
  path already uses for a failed host.

**Risk.** Medium-high. Graph churn is manageable; the latency story is not, and there
is no interface here with spare I/O to validate a hardware loop.

**User value.** Medium. Software effects are already covered by the plugin rack. The
genuine gain is hardware outboard and out-of-process chains (a separate Carla or
Ardour instance), which is a real but narrow audience.

**Verdict.** Worth doing after the control features, and only with an explicit
"uncompensated, dry fallback" position written down first.

---

## 6. Surround upmix

**What it would take.** Stereo → 5.1/7.1: derive centre from the mid signal, LFE from
a low-passed sum, and surrounds from a decorrelated difference with some delay and
filtering. The DSP itself is a new descriptor in `native/dsp_ladspa.cpp` beside the
existing gain, mixer, limiter and FX descriptors — modest work, and the file's existing
conventions cover the port and teardown details that `verify_dsp.cpp` already asserts.

The integration is where the cost is:

- Bus outputs become 6 or 8 channels. `audio.position` already supports this — the
  composite path declares `FL FR FC LFE RL RR SL SR` — so the declaration is not the
  problem.
- `synchronizeLinks()` links only `FL` and `FR` from each bus output to the hardware
  sink. A surround bus needs all six or eight channels linked, and device selection has
  to reject stereo sinks instead of silently dropping the extra channels.
- The bus limiter is stereo, one linked pair. A surround bus needs three or four
  stereo limiters, or the limiter has to move before the upmix and stop being a true
  output limiter. The composite path already demonstrates the per-pair approach, so
  this is a design fork rather than new invention — but it is a fork.

**Risk.** Medium. The DSP is the easy part; the validation is not. There is no
multichannel output device in the validation record, and "the upmix sounds right" is a
listening judgement that cannot be made with synthetic graphs.

**User value.** Low to medium. This is a voice and streaming mixer; 5.1 upmix is a
home-theatre nicety, and the existing eight-channel composite path already covers
discrete multichannel recording. The README is also careful to say the Position pad is
"not true surround", and that distinction should survive this feature rather than be
blurred by it.

**Verdict.** Do last, or not at all. It is the item whose value least matches its
validation cost.

---

## 7. Full parametric master EQ

**What it would take.** The graph work is the cheapest of the seven. `bq_peaking`,
`bq_highshelf` and `bq_lowshelf` are already used as builtin filter-chain labels for
the three virtual-input bands, so a chain of N biquads per bus is a change inside
`matrixConfiguration()` and nothing else. Live editing of gain follows the existing
`pw-cli set-param` path in `RoutingMatrix::apply()`, which already updates
`leftEq%1:Gain` without restarting audio. Band *count* is fixed at graph build, so
changing it restarts audio — the same trade the README already accepts for mode
changes.

The cost is the UI and the model, not the audio:

- A real curve editor: draggable band handles, type/frequency/Q/gain per band, numeric
  entry, per-band bypass, and a curve that matches what the biquads actually do.
- A settings schema that migrates without breaking the existing `eq[3][6]` array for
  the virtual strips.
- A placement decision. An EQ on the bus is not only a monitoring tool: buses feed
  B1–B3 and the recorder, so a "master EQ" changes what other applications and the
  recording receive, not just what the user hears. Pre- or post-limiter is a real
  question with audible consequences.

**Risk.** Low to medium. Low on the audio path, medium on the UI and on getting the
parameter model right the first time.

**User value.** Medium. Useful for room and voice correction and for taming a difficult
microphone, but the per-strip three-band already covers most of that, and a full
parametric master EQ is the least-requested of the seven for a voice mixer.

**Verdict.** The best value-per-line-of-graph-code, and the worst value-per-line-of-UI.
A reasonable second or third feature.

---

## Recommendation: build macro control first

### Why this one

1. **It is the only item that adds no risk to the audio path.** No new node, no graph
   version bump, no new dependency, no change to `libmixer_dsp.so`. After a stability
   release, the first feature should not be one that can crackle.
2. **The test story is incremental, not new.** `ControlCommand`, `applyControlCommand`
   and `drainControlCommands` are already exercised by the `control-commands` ctest
   case. Macros extend that vocabulary; they do not require a new harness or a new
   class of test.
3. **It sequences the next feature.** MIDI is mostly "a CC maps to an action". Once
   macros exist, that action is a macro, and MIDI becomes a binding layer rather than a
   second control system. Choosing macros first gets two items closer for the price of
   one.
4. **It multiplies what already ships.** `--ctl` currently does one parameter per
   keybinding. Macros turn it into one key per situation, which is the workflow the
   README already recommends to Wayland users.
5. **It is fully verifiable here.** Every part — parsing, validation, application,
   persistence, and the fact that all steps land in one graph update — can be checked
   headlessly, in the style the repository already uses.

Runner-up is MIDI (higher ceiling, but needs a dependency decision and hardware this
machine does not have). Third is the parametric master EQ (cheapest graph change,
heaviest UI).

### Work breakdown

Not dated. Each phase is independently reviewable, and phases 1–3 are useful without a
UI.

**Phase 1 — model and persistence.**
`MacroSettings` holding a list of named macros; each step reuses the existing
`ControlCommand` verb/kind/index/mode/db fields so parsing and clamping are shared
rather than duplicated. Persist under a `"macros"` key in `settings.json`, following
the existing `load()` conventions: absent key means no macros, unknown keys are
ignored, malformed entries are dropped individually with a message rather than
discarding the whole settings file. No migration is needed for existing profiles.

**Phase 2 — validation.**
A macro is validated as a unit at load and at edit time. Every step must parse, every
target must exist, and verbs that would force a graph rebuild are refused with a
specific error. A macro with one bad step is rejected entirely; it is never applied
partially. `toggle` is accepted but marked, so the editor can warn that running the
macro twice flips twice. This phase is pure functions over `Settings`, so it is
testable without a window.

**Phase 3 — execution.**
`runMacro(const Macro&, Settings&, QString* error)` applies each step through
`applyControlCommand()` into the caller's `Settings`, stops at the first failure and
reports which step failed, and leaves the caller to issue a single `engine.apply()`
and `engine.save()`. The single-apply discipline is the whole point and should be
stated in the header comment, because a later refactor that calls `apply()` per step
would silently break atomicity without failing any existing test.

**Phase 4 — triggers.**
Extend `parseControlCommand` with a `Macro` verb and a macro name, routed through the
existing on-disk queue, so `linux-mixer --ctl macro <name>` works from a desktop
keybinding. Exit codes stay as they are: 2 for invalid input or an unknown macro, 1 if
no mixer is running. Add a Macro panel with one button per macro for in-window use.
Global hotkeys remain out of scope, for the same Wayland reason the README already
documents.

**Phase 5 — UI.**
A plain editor: macro list, step rows, Run and Test buttons, and a status line
reporting the first failing step after a run. Reuse the dialog conventions from
`profile_ui.cpp` and `recorder_ui.cpp`; no new visual language, no new icons.

**Phase 6 — documentation.**
`docs/MACROS.md` in the register of `docs/RECORDER.md`: what a macro is, the step list,
the `toggle` caveat, the ~700 ms latency when triggered through `--ctl`, the fact that
macros cannot change devices or modes, and an explicit statement that nothing here is
synchronised to tempo or to audio position.

### Test plan

Consistent with the existing ctest cases and Python integration checks.

**New ctest case `macro-commands`.** Registered exactly like `control-commands`:
`add_test(NAME macro-commands COMMAND linux-mixer --macro-test)` with
`QT_QPA_PLATFORM=offscreen` and `TIMEOUT 30`, driven from `tests/verify_macros.cpp`
using the same `CHECK(...)` macro style as `tests/verify_control.cpp`. It covers:

- parsing of a well-formed macro, including name and step order;
- clamping of out-of-range gain values, matching the `-60..+12` behaviour already
  asserted for `gain`;
- rejection of an unknown target, an unknown verb, an empty step list and a duplicate
  macro name;
- whole-macro rejection when a single step is invalid, with the valid steps
  demonstrably unapplied;
- persistence: `Settings::json()` → `load()` round-trip preserves macros, and a
  settings object with no `"macros"` key loads cleanly (legacy profile);
- refusal of steps that require a graph rebuild (for example an output-device change),
  asserted on the error string rather than on a crash.

**Extend `tests/verify_control.cpp`.** Only for the CLI surface: `--ctl macro` argument
parsing, exit code 2 for an unknown macro name, and the existing four verbs continuing
to behave unchanged. The existing assertions are not modified.

**New integration check `tests/verify_macros.py`.** Modelled on `verify_matrix.py`:
start the controller against a temporary profile, define a two-step macro
(`gain mic1 -6`, `mute music on`), trigger it, and read back the values the same way
`verify_matrix.py` does — asserting both landed before the first poll after the
trigger. This is the test that catches a macro applied across two apply cycles, which
is the one behaviour the unit tests cannot see. It also runs the negative case: a macro
whose second step is invalid must leave the first step unapplied.

**Explicitly not tested, and to be reported as such.** Real controller or keyboard
triggering; behaviour under a burst of macro runs inside one 700 ms poll window, where
the queue's `QSet` deduplication and the one-file-per-command model have not been
exercised for this pattern; and the audible result of a macro that changes several
faders at once, which is a listening judgement. A deliberate burst case should be added
before release rather than discovered after it.

### Open questions to settle before Phase 4

- Whether macros should eventually change routes and sends. That needs new verbs, and
  route changes can restart audio, which breaks the atomicity argument — so probably
  not, but the decision should be written down rather than assumed.
- Whether macros get persistent on/off state (Voicemeeter's MacroButtons have it).
  Recommend deferring: state turns a macro into a small program, and a small program
  needs the conditionals and loops that Phase 1 deliberately excludes.
- Whether the profile restore review in `docs/PROFILES.md` should list macros
  explicitly. They live in `settings.json`, so they travel with a profile bundle
  already; the review text may still deserve a line.

## What this document does not do

It does not bump the version, change `CMakeLists.txt`, add tests, or modify any source
file. It does not commit the project to any of the seven items, and it does not
schedule the recommended one. Its only claim is that macro control is the cheapest
honest first step, and that ASIO should stop being described as a missing feature.
