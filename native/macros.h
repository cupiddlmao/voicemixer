#pragma once
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>

struct ControlCommand;

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
// Validates a macro as a unit: non-empty name, at least one step, every step
// parses, and the name is unused by `existing` (case-insensitively).
bool validateMacro(const Macro& macro, const QList<Macro>& existing, QString& error);

// Included last so ControlCommand is complete for the step lists above. This
// header stays safe to include before control_commands.h and engine.h.
#include "control_commands.h"
