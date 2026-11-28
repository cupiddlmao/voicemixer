#pragma once
#include <QtCore/QList>
#include <QtCore/QMutex>
#include <QtCore/QString>
#include <QtCore/QThread>
#include <functional>
#include <set>
#include <utility>
#include "midi.h"

// MIDI input transport (Phase 3): where those bindings actually get pressed.
//
// This file adds two things and nothing else: a reader that receives MIDI from the
// ALSA sequencer on its own thread, and the pure mapping from one received message
// onto the actions a binding asks for. It does not start or stop anything by
// itself, and it touches no Settings, no engine and no widget — a later phase owns
// those, which is why main.cpp and console.cpp are untouched here.
//
// The four decisions this phase rests on, recorded here because Phases 4-5 depend
// on them and would otherwise have to re-argue them:
//
// 1. Transport is the ALSA sequencer through libasound (snd_seq), not raw
//    /dev/snd/midiC*D* and not a PipeWire MIDI stream. The reader opens its own
//    client named "Linux Mixer" and one input port named "Linux Mixer MIDI"; other
//    clients send to that port. Sources are discovered by walking the sequencer's
//    port list and subscribing to every port that can be a source, and on a
//    ports-changed event (client or port start/exit) that subscription set is
//    rebuilt, which is what gives hotplug without polling device nodes.
//    libasound2-dev is already a build requirement, so this adds no package, and
//    the sequencer supplies stable client/port names that raw nodes do not.
//    PipeWire is deliberately not linked: a control event is a few bytes on an
//    established subscription and does not need the spawn-a-helper pattern the
//    metering and recording paths use for streams.
//
// 2. Threading follows the meter readers exactly. The reader is a QThread; the
//    thread owns the sequencer handle and copies of what it needs; results leave
//    the thread the same way MeterReaders' peaks do — through a mutex-protected
//    queue that the other side drains with takeActions() — never by touching
//    Settings, the engine or a widget from here. A delivery hook exists for a
//    caller that wants to be told immediately, and it runs on the reader thread:
//    a later phase must marshal it (a queued connection or equivalent) before it
//    applies a command, exactly as the meter precedent does with its peaks.
//
// 3. Mapping is a pure function over the bindings and one message, so the reader,
//    a later UI preview and the tests all use the same code and none of them needs
//    an ALSA device or a window. The rules:
//      * CC (SND_SEQ_EVENT_CONTROLLER) bound to a gain action: the stored value is
//        ignored and the CC value v maps linearly onto the whole clamped range,
//        db = -60 + (v/127)*72, which is the CLI's -60..+12 clamp. This is the
//        whole-range mapping; partial-range maps are a later refinement.
//      * CC bound to mute, bypass or denoise: the stored verb and target are kept
//        and the stored mode is ignored — v >= 64 is on, v < 64 is off. That is
//        what makes a fader-to-switch mapping usable without a saved mode.
//      * Note-on (SND_SEQ_EVENT_NOTEON) with a velocity above zero: the stored
//        action runs exactly as parsed, mode included, so "toggle" behaves as a
//        button press. Velocity 0 is a note-off in disguise and is ignored.
//      * CC bound to a macro action: the macro triggers when v >= 64.
//      * Channel: a binding with channel -1 matches any channel; otherwise the
//        event channel must equal the binding channel.
//
// 4. Several bindings may match one event, and that is deliberate (one CC moving a
//    fader and firing a macro is a normal setup). One result per matching binding
//    is delivered, in the order the bindings are stored, so a caller's order is
//    the file's order. One event matching nothing is normal, not an error: no
//    queue entry and no hook call.

// One action ready to run, with the id of the binding that asked for it. The id is
// carried so a later UI and a test can say which binding fired without comparing
// commands; the command itself is fully resolved (gain value or switch mode
// already decided), so a caller never re-applies a mapping rule.
struct MidiAction {
    QString bindingId;
    ControlCommand command;
};

// The gain a CC value means under the whole-range mapping: 0 is -60 dB and 127 is
// +12 dB, the CLI's clamp. Values outside 0-127 are not extended or clamped here;
// mapMidiEvent() refuses such a message, since a sequencer never produces one.
double midiCcGainDb(int value);

// Every action one received message should run, in stored-binding order. Pure: no
// Settings, no engine, no ALSA type, so tests and a later UI preview call this
// directly. A message class, channel, number or value outside the wire ranges is
// malformed and matches nothing rather than being repaired into a near miss.
QList<MidiAction> mapMidiEvent(const MidiBindings& bindings, MidiBinding::Message message,
                               int channel, int number, int value);

// libasound's own forward declaration, so this header does not pull in
// <alsa/asoundlib.h> for its callers (snd_seq_t is a typedef of this struct).
struct _snd_seq;

// An ALSA sequencer client named "Linux Mixer" with one input port named
// "Linux Mixer MIDI", running on its own thread. open() creates the client, port
// and thread and starts listening; close() stops the thread and releases the
// handle. Handles and failures live behind a mutex because open/close/setBindings
// are called from the owning thread while the thread is reading.
class MidiInput:public QThread {
public:
    // Called on the reader thread with each delivered batch, after the batch has
    // been queued for takeActions(). It must not block, touch Settings or the
    // engine, or paint; keep it to a marshal.
    using ActionHandler=std::function<void(const QList<MidiAction>&)>;
private:
    // _snd_seq rather than <alsa/asoundlib.h> here: this header is included by
    // tests and by a later UI, and neither should need the ALSA headers to name
    // the class. The .cpp includes the real one.
    _snd_seq* sequence=nullptr;
    int clientIdValue=-1;
    int portIdValue=-1;
    // Only the reader thread touches this, so it needs no lock: it exists purely
    // to avoid re-subscribing to the same source on every ports-changed event.
    std::set<std::pair<int,int>> subscribed;
    QMutex mutex;
    QList<MidiAction> pending;
    MidiBindings bindings;
    ActionHandler handler;
    QString failure;
    void run()override;
    void receiveEvents();
    void handleEvent(const struct snd_seq_event& event);
    void rebuildSubscriptions();
    void deliver(MidiBinding::Message message,int channel,int number,int value);
    void setFailure(const QString& text);
public:
    ~MidiInput()override;
    // False with a clear message when libasound cannot open a client — the path a
    // machine without ALSA, such as CI, takes — or when the port cannot be
    // created. True means the client and port exist and the thread is listening.
    bool open(QString& error);
    void close();
    // The sequencer ids of the client and port, or -1 when closed. A later phase
    // shows them and a test subscribes to them.
    int clientId()const{return clientIdValue;}
    int portId()const{return portIdValue;}
    // A copy under the lock, so the reader never races an edit. Mapping reads the
    // copy, never Settings.
    void setBindings(const MidiBindings& value);
    void setHandler(ActionHandler value);
    // Drains what has been delivered since the last call. The queue is bounded by
    // the caller's patience rather than a fixed cap: MIDI is a handful of bytes per
    // event and an event only queues when it matched a binding.
    QList<MidiAction> takeActions();
    QString takeError();
};
