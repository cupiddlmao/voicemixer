#pragma once
#include "engine.h"
#include <QtCore/QString>
#include <QtCore/QStringList>

// Remote control of a running mixer, e.g. `linux-mixer --ctl mute mic1 toggle`.
// Targets: mic1..mic5, system, music, aux, a1..a5, b1..b3.
// Verbs: mute [on|off|toggle], gain <dB>, bypass|denoise <mic> [on|off|toggle].
struct ControlCommand {
    enum Verb { Mute, Gain, Bypass, Denoise } verb=Mute;
    enum Kind { Mic, Input, Bus } kind=Mic;
    int index=0;
    enum Mode { On, Off, Toggle } mode=Toggle;
    double db=0;
};
bool parseControlCommand(const QStringList& words,ControlCommand& command,QString& error);
bool applyControlCommand(Settings& settings,const ControlCommand& command,QString& error);
// Client side: queue words for the running controller. Returns a process exit code.
int sendControlCommand(const QStringList& words,bool controllerRunning,const QString& runtimeDirectory);
// Controller side: apply and remove queued commands; returns how many changed settings.
int drainControlCommands(const QString& runtimeDirectory,Settings& settings,QStringList* errors=nullptr);
