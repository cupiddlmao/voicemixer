# Macro control

> **Planned for a future 0.6.0 release.** Macros are a draft design, not a shipped
> feature: nothing described in this file exists in any released version of
> Voicemixer yet.

This document follows the plan in [the 0.6.0 feature plan](PLAN-0.6.0.md), sections 4
and Phase 6. It describes behaviour that is specified but not implemented, in the
same register as [the recorder notes](RECORDER.md). Where something is unverified,
it says so rather than promising it.

## What a macro is

A macro is a named, ordered list of steps. Each step is the same
`{verb, target, value}` triple the command line already accepts, so a macro is the
existing `--ctl` vocabulary collected under one name.

Running a macro applies every step to the same settings object and then issues a
single `engine.apply()`. That call is coalesced onto a 20 ms timer, so all of the
steps reach the audio graph in one update. From the graph's point of view the macro
is applied at once: there is no audible intermediate state where the microphone is
already pulled back but the music is not yet muted.

That property is the whole point of the feature and is deliberately protected. A
macro is not a fast sequence of keypresses, and an implementation that called
`apply()` once per step would silently be one.

A macro has no state of its own. It is a list of settings changes; it does not
remember whether it was last run, and it does not toggle a flag somewhere to say so.

## Steps

The four existing verbs:

- `mute` — `on`, `off` or `toggle`
- `gain` — dB, clamped to `-60..+12` like the command line
- `bypass` — `on`, `off` or `toggle`, microphones only
- `denoise` — `on`, `off` or `toggle`, microphones only

Their targets are the command line's target list: `mic1`–`mic5`, `system`, `music`,
`aux`, `a1`–`a5` and `b1`–`b3`.

A macro is validated as a unit, both when it is loaded and when it is edited. Every
step must parse, every target must exist, and a macro containing one bad step is
rejected entirely — it is never applied in part. If a run does fail part-way
through, it stops at the first failing step and reports which step that was.

`toggle` is accepted, but the editor marks the step and warns about it. See below.

## The `toggle` caveat

Running a macro twice flips twice.

A `toggle` step resolves against whatever the value happens to be when the macro
runs. It is not "turn it on"; it is "change it". So a macro containing
`mute music toggle` will mute music the first time and unmute it the second time,
and the person who bound it to a key is likely to want only the first behaviour.

This is usually not what someone means by a macro. Bind `on` or `off` explicitly
unless you genuinely want a switch. The editor flags `toggle` steps for that reason,
but it does not prevent them: a switch is occasionally the intent.

The same applies to a macro whose `toggle` target is changed by hand between runs.
The macro has no memory, so it cannot know the strip was muted by someone else.

## Triggering a macro

Macros are triggered through the existing command-line queue:

```bash
linux-mixer --ctl macro <name>
```

That writes a command file the running controller reads on its existing poll, which
means the macro takes effect within roughly 700 ms rather than instantly. The delay
is the poll interval, not the macro: once the command is read, every step lands in
one graph update as described above. Binding a macro to a key does not make it
faster, and it does not make it slower than the single-parameter commands the README
already recommends.

Exit codes are unchanged from the rest of `--ctl`: 2 for invalid input or an unknown
macro name, and 1 if no mixer is running.

There is also a small Macro panel with one button per macro, for use inside the
window. It does not go through the command file, so the ~700 ms wait is specific to
the `--ctl` path.

**Global hotkeys remain out of scope.** A background application cannot reliably
grab and receive system-wide key combinations on Wayland, which is the same reason
the README points at `--ctl` plus desktop keybindings instead of a hotkey system.
Macros do not change that; they make the desktop-keybinding route worth using,
because one key can now express a whole situation instead of one parameter.

## What a macro cannot do

Macros cannot change devices, routes or modes. That is a deliberate restriction, not
a gap awaiting a patch.

Anything that alters the set of nodes in the audio graph forces the engine to
restart, and a restart destroys the single-update guarantee that makes a macro a
macro. So these are refused with a specific error:

- choosing or changing an output device, or a hardware strip's device and channel;
- switching a bus between Normal and Composite, or editing a Composite patch;
- changing the sample rate, or any other setting that rebuilds the configuration;
- changing routes and sends, moving an application between System, Music and AUX,
  and editing saved application rules.

A later version could add route verbs, but route changes can restart audio, so it is
not a small addition — the atomicity claim would have to be weakened or the verb
restricted. That decision is not made here, and this document does not promise it.

There are also no ramps and no delays. A step is a value, not a move over time. The
mixer's existing smoothing is a 10 ms per-parameter fade, not a time-parameterised
transition, so a "fade mic1 to −12 dB over two seconds" step would need either
DSP-level targets or a controller-side timer issuing many small commands — and the
latter fights the apply coalescing and steps audibly. Macros do not do it.

There are no conditionals, no state variables, no loops, and no macro that calls
another macro.

## Not synchronised to anything

Nothing here is synchronised to tempo or to audio position.

A macro is not a musical event. It does not read a clock, a MIDI timecode, a
transport position, or the position of anything playing. It has no quantisation, no
bar or beat alignment, and no notion of where the audio currently is. A macro
triggered at 3.7 seconds into a track runs at 3.7 seconds, whenever the command was
read — up to a poll interval late, and never nudged onto a grid to hide that.

If a macro is triggered by a key press while audio is playing, the only relationship
between the two is that a person pressed the key at roughly that moment.

## Verification and scope

The planned checks are the ones the repository already uses for control:
a `macro-commands` ctest case for parsing, clamping, whole-macro rejection and the
settings round-trip, extension of the existing control test for the `--ctl macro`
surface, and an integration script that triggers a two-step macro against a real
engine and reads the values back, asserting both landed before the first poll after
the trigger. That last check is the only one that can see a macro applied across two
apply cycles.

Three things are not covered and should be reported as untested rather than as
working: real keyboard or controller triggering, a burst of macro runs inside one
700 ms poll window, and the audible result of a macro that moves several faders at
once. The last is a listening judgement, not something a synthetic graph can
measure.

This draft does not implement macros, does not change the version, and does not
commit the project to shipping them in 0.6.0.
