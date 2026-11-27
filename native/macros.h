#pragma once
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>

struct ControlCommand;
struct Settings;

// A named, ordered list of control steps. A step is the same ControlCommand the
// --ctl queue uses, so verbs, targets, modes and the -60..+12 gain clamp are
// shared rather than reimplemented here.
struct Macro {
    QString name;
    QList<ControlCommand> steps;
    QJsonObject json() const;
    // Reads {"name": "...", "steps": ["gain mic1 -6", ...]}. A macro with one
    // unparsable step is rejected as a whole and left untouched.
    bool load(const QJsonObject& object, QString& error);
};

// Macros stored under the "macros" key of settings.json. A malformed macro is
// dropped on its own and described in `errors`; the rest of the file still loads.
struct MacroSettings {
    QList<Macro> macros;
    QStringList errors;
    QJsonArray json() const;
    void load(const QJsonObject& settings);
};

// Parses one step through parseControlCommand, so clamping matches the CLI.
bool parseMacroStep(const QString& text, ControlCommand& step, QString& error);
// Canonical step text, e.g. "gain mic1 -6"; parsing it again is a no-op.
QString macroStepText(const ControlCommand& step);
// Validates a macro as a unit: non-empty name, at least one step, a name unused
// by `existing` (case-insensitively), and every step individually sound — the
// target exists, the verb applies to that target, and the verb is one a macro
// may run. One bad step rejects the whole macro; validation never stops short
// and never accepts a partially valid list.
bool validateMacro(const Macro& macro, const QList<Macro>& existing, QString& error);

// Included last so ControlCommand is complete for the step lists above. This
// header stays safe to include before control_commands.h and engine.h.
#include "control_commands.h"

// What applying a verb costs the audio graph. Parameter verbs (mute, gain,
// bypass, denoise) reach the graph through one coalesced RoutingMatrix::apply().
// GraphRebuild verbs (device, mode or sample-rate selection, if such verbs are
// ever added) restart the engine, so a macro refuses them rather than break its
// single-update promise. Classification is by verb class here, not by a list of
// allowed commands in the validator.
enum class CommandScope { Parameter, GraphRebuild };
CommandScope commandScope(ControlCommand::Verb verb);

// True when any step is a toggle, so an editor can warn that running the macro
// twice flips twice. Computed on demand, so it cannot go stale after an edit.
bool macroContainsToggle(const Macro& macro);

// Applies every step of `macro` to `settings` through applyControlCommand,
// stopping at the first failure and naming that step in `error`.
//
// Atomicity: the steps run against a copy that replaces `settings` only when all
// of them succeed, so a failure leaves the caller's settings exactly as they
// were and a successful run never exposes an intermediate state. This function
// deliberately does not touch the engine: the caller issues exactly one
// engine.apply() and engine.save() after a successful run. That single apply is
// what lands the whole macro in one coalesced graph update, so this must not be
// refactored into an apply()-per-step loop.
bool runMacro(const Macro& macro, Settings& settings, QString* error);
