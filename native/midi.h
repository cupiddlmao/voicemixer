#pragma once
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>

// MIDI control bindings: one message, one action, stored under the "midi" key of
// settings.json beside "macros" and "appRules".
//
// Two decisions this model rests on, recorded here because Phases 3-5 depend on
// them and would otherwise have to re-argue them:
//
// * The input transport will be the ALSA sequencer through libasound, not raw
//   /dev/snd/midiC*D* and not a PipeWire MIDI stream behind a helper process.
//   libasound2-dev is already a build requirement, so this is not a new package;
//   the sequencer supplies hotplug and stable client/port names, which raw device
//   nodes do not; and a control event is a few bytes on an established
//   subscription, so it does not need the spawn-a-helper pattern the metering and
//   recording paths use for streams.
//
// * MIDI events will arrive on the sequencer's own thread in that later phase,
//   never on the GUI thread. Nothing in this model may therefore depend on thread
//   context: a binding is a plain value, loading and validation are free
//   functions over data, and the caller marshals an event onto the GUI thread
//   before it touches Settings or the engine. A model that quietly assumed the
//   GUI thread would compile cleanly and then race the first real fader move.

// Settings holds this collection by value, so MidiBindings must be complete even
// when engine.h is reached from inside control_commands.h (which this header
// pulls in, and which pulls in engine.h in turn). It is therefore declared first,
// over a forward-declared MidiBinding, the same way macros.h can name its step
// list before ControlCommand is defined. The member is a QList, which needs no
// complete element type until its methods are instantiated; json() and load()
// below are out of line in midi.cpp for the same reason.
struct MidiBinding;

// Bindings stored under the "midi" key of settings.json. An absent key means no
// bindings, which is what every settings file written before this feature says. A
// malformed binding is dropped on its own and described in `errors`; the rest of
// the file still loads. Duplicate ids are the one exception: ids are how a binding
// is addressed, so a file that repeats one is not trusted and loads empty rather
// than picking whichever entry came first.
struct MidiBindings {
    QList<MidiBinding> bindings;
    QStringList errors;
    QJsonArray json() const;
    // Reads the settings root, not the "midi" value, so a caller can hand it the
    // same QJsonObject Engine reads settings.json into.
    void load(const QJsonObject& settings);
};

// A binding holds its action by value, so ControlCommand must be complete here.
// This header therefore pulls in control_commands.h and, through it, engine.h and
// Settings; it is not the lightweight forward-declaring header macros.h is.
#include "control_commands.h"

// One CC or note-on message mapped to one action. The action is either a single
// ControlCommand — the mute/gain/bypass/denoise verbs and their targets, the same
// shape a macro step has — or, when that command's verb is Macro, the name of a
// saved macro. Nothing else is stored: no ranges, no ramps, no curves. Absolute
// CC mapping, relative encoders and 14-bit pairs add message classes in a later
// phase and must not need a saved file rewritten.
struct MidiBinding {
    enum Message { ControlChange, NoteOn };
    // Stable identity for an entry. An editor, and later learn mode, address a
    // binding by it, so two entries sharing one id are a conflict rather than a
    // second mapping.
    QString id;
    Message message=ControlChange;
    // 0-15, or -1 for any channel: a one-knob mapping then works whatever channel
    // the device happens to transmit on, without being rewritten per device.
    int channel=-1;
    // Controller or note number, 0-127.
    int number=0;
    ControlCommand action;
    // False only for a hand-built binding. A default ControlCommand is a valid
    // command ("mute mic1 toggle"), so without this flag a binding whose action was
    // never set would silently mute a microphone. load() always sets it.
    bool hasAction=false;
    QJsonObject json() const;
    // Reads {"id": "...", "message": "cc"|"noteon", "channel": -1..15,
    // "number": 0..127, "action": "gain mic1 -6"|"macro Stream"}. Unknown keys are
    // ignored. Channel and number are required: a missing one is a hand-edit
    // mistake that would otherwise become a binding on channel 0, controller 0
    // that silently does nothing.
    bool load(const QJsonObject& object, QString& error);
};

// "cc" and "noteon" as stored, and back. Unknown text is an error, never a guess.
QString midiMessageText(MidiBinding::Message message);
bool parseMidiMessage(const QString& text, MidiBinding::Message& message, QString& error);

// One action as canonical text: "gain mic1 -6", "mute music on", "macro Stream".
// Parsing goes through parseControlCommand, the same parser the --ctl queue and
// macro steps use, so the gain clamp and the target spellings are shared rather
// than restated here.
QString midiActionText(const ControlCommand& action);
bool parseMidiAction(const QString& text, ControlCommand& action, QString& error);

// Validates one binding without applying it: a non-empty id, a channel in -1..15,
// a controller or note number in 0..127, an id no entry in `existing` already
// uses, and an action that exists. "Exists" means: a macro action names a macro
// saved in `settings.macros`, and a command action uses a target that exists and
// a verb a binding may run. Pure over Settings, so it needs no window and no
// engine; the macro lookup is the only reason it reads Settings rather than a
// list.
//
// The split from load() is deliberate. load() decides whether an entry is
// well formed text; this decides whether the thing that text names exists. A
// binding naming a macro that has since been deleted therefore still parses, and
// is refused here with the macro's name in the message, rather than disappearing
// silently at load.
bool validateMidiBinding(const MidiBinding& binding, const QList<MidiBinding>& existing,
                         const Settings& settings, QString& error);
// Validates a whole list in order with the same per-binding rules, reporting the
// first refusal and clearing `error` when every binding is sound. An editor saves
// through this, and a load path reports through it once the file has been parsed.
bool validateMidiBindings(const QList<MidiBinding>& bindings, const Settings& settings, QString& error);
