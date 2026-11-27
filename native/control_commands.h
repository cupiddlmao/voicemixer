#pragma once
#include <QtCore/QString>
#include <QtCore/QStringList>

struct Settings;

// Remote control of a running mixer, e.g. `linux-mixer --ctl mute mic1 toggle`.
// Targets: mic1..mic5, system, music, aux, a1..a5, b1..b3.
// Verbs: mute [on|off|toggle], gain <dB>, bypass|denoise <mic> [on|off|toggle],
//        macro <name> (runs a macro from the saved settings).
struct ControlCommand {
    enum Verb { Mute, Gain, Bypass, Denoise, Macro } verb=Mute;
    enum Kind { Mic, Input, Bus } kind=Mic;
    int index=0;
    enum Mode { On, Off, Toggle } mode=Toggle;
    double db=0;
    // The macro to run; only meaningful when verb is Macro, where it replaces
    // the target/value fields.
    QString name;
};
bool parseControlCommand(const QStringList& words,ControlCommand& command,QString& error);
bool applyControlCommand(Settings& settings,const ControlCommand& command,QString& error);
// Client side: queue words for the running controller. Returns a process exit code.
int sendControlCommand(const QStringList& words,bool controllerRunning,const QString& runtimeDirectory);
// Controller side: apply and remove queued commands; returns how many changed settings.
// A queued `macro` runs the named macro through runMacro and counts as one command, so
// the caller still issues a single engine.apply() and engine.save() for the whole drain.
int drainControlCommands(const QString& runtimeDirectory,Settings& settings,QStringList* errors=nullptr);

// Existing callers include this header alone and rely on Settings being complete.
#include "engine.h"
