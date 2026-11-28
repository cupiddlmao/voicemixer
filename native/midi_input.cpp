#include "midi_input.h"
#include <alsa/asoundlib.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <vector>

// Mapping -------------------------------------------------------------------
//
// Both functions are pure and live beside the model, so the reader, a later UI
// preview and the tests share one definition of what a CC value means. Nothing
// here reads Settings, the engine or a device.

double midiCcGainDb(int value) {
    // The whole-range mapping recorded in the header: 0 -> -60 dB, 127 -> +12 dB,
    // which is the clamp parseControlCommand applies to a stored gain. It is
    // deliberately a full-range map; a binding with its own min/max is the later
    // refinement the model was left able to grow into.
    return -60.0+double(value)/127.0*72.0;
}

QList<MidiAction> mapMidiEvent(const MidiBindings& bindings,MidiBinding::Message message,
                               int channel,int number,int value) {
    QList<MidiAction> actions;
    // A sequencer cannot produce these; a caller or a test can. Refuse rather
    // than clamp, so a malformed event is visibly nothing instead of a plausible
    // near miss (clamping value 200 to full gain would look like a real press).
    if(channel<0||channel>15||number<0||number>127||value<0||value>127)return actions;
    for(const auto& binding:bindings.bindings){
        // A hand-built binding whose action was never set is not a command: the
        // default ControlCommand would silently mute mic1.
        if(!binding.hasAction)continue;
        if(binding.message!=message)continue;
        if(binding.channel!=-1&&binding.channel!=channel)continue;
        if(binding.number!=number)continue;
        ControlCommand command=binding.action;
        if(message==MidiBinding::NoteOn){
            // Velocity 0 is the wire's note-off. The stored action runs as parsed,
            // mode included, so "toggle" is a button press and "on" is a hold.
            if(value==0)continue;
        }else{
            switch(command.verb){
            case ControlCommand::Gain:
                // The stored dB is the binding's default, not its target: the CC
                // is the position, and every value must move the fader.
                command.db=midiCcGainDb(value);
                break;
            case ControlCommand::Mute:
            case ControlCommand::Bypass:
            case ControlCommand::Denoise:
                // Verb and target stay as stored; only the mode follows the
                // switch. 64 is the MIDI convention for "on".
                command.mode=value>=64?ControlCommand::On:ControlCommand::Off;
                break;
            case ControlCommand::Macro:
                // A macro has no mode: its trigger point is the whole mapping.
                if(value<64)continue;
                break;
            }
        }
        actions.append(MidiAction{binding.id,command});
    }
    return actions;
}

// Reader --------------------------------------------------------------------

MidiInput::~MidiInput() { close(); }

void MidiInput::setFailure(const QString& text) {
    QMutexLocker lock(&mutex);
    failure=text;
}

QString MidiInput::takeError() {
    QMutexLocker lock(&mutex);
    QString value;value.swap(failure);return value;
}

void MidiInput::setBindings(const MidiBindings& value) {
    QMutexLocker lock(&mutex);
    bindings=value;
}

void MidiInput::setHandler(ActionHandler value) {
    QMutexLocker lock(&mutex);
    handler=std::move(value);
}

QList<MidiAction> MidiInput::takeActions() {
    QMutexLocker lock(&mutex);
    QList<MidiAction> ready;ready.swap(pending);return ready;
}

bool MidiInput::open(QString& error) {
    error.clear();
    if(sequence){error=QStringLiteral("MIDI input is already open");return false;}
    snd_seq_t* handle=nullptr;
    // "default" is the sequencer device, not a sound card: this opens /dev/snd/seq.
    // A machine without ALSA (CI) fails here, which is why open() reports the
    // reason instead of starting a thread that would fail invisibly.
    int result=snd_seq_open(&handle,"default",SND_SEQ_OPEN_INPUT,0);
    if(result<0){
        error=QStringLiteral("cannot open the ALSA sequencer: %1").arg(QString::fromUtf8(snd_strerror(result)));
        return false;
    }
    // The two names are part of the contract: one client, one port, both stable so
    // the user can connect a controller in a patchbay and keep it across restarts.
    result=snd_seq_set_client_name(handle,"Linux Mixer");
    if(result<0){
        error=QStringLiteral("cannot name the ALSA sequencer client: %1").arg(QString::fromUtf8(snd_strerror(result)));
        snd_seq_close(handle);return false;
    }
    // CAP_WRITE (not CAP_READ) is what makes this port a destination: other clients
    // send to it, we read from it. A port that could only be a source would accept
    // a subscription and deliver nothing.
    const int created=snd_seq_create_simple_port(handle,"Linux Mixer MIDI",
        SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC|SND_SEQ_PORT_TYPE_APPLICATION);
    if(created<0){
        error=QStringLiteral("cannot create the MIDI input port: %1").arg(QString::fromUtf8(snd_strerror(created)));
        snd_seq_close(handle);return false;
    }
    sequence=handle;
    clientIdValue=snd_seq_client_id(handle);
    portIdValue=created;
    // Non-blocking so the thread can check for interruption between batches instead
    // of parking inside snd_seq_event_input() and delaying close().
    snd_seq_nonblock(handle,1);
    {QMutexLocker lock(&mutex);failure.clear();pending.clear();}
    QThread::start();
    return true;
}

void MidiInput::close() {
    // The thread owns the handle while it runs, so it stops first and the handle is
    // released only after wait() confirms it is gone.
    if(isRunning()){requestInterruption();wait();}
    if(sequence){snd_seq_close(sequence);sequence=nullptr;}
    clientIdValue=-1;portIdValue=-1;
    subscribed.clear();
    QMutexLocker lock(&mutex);
    pending.clear();
}

void MidiInput::run() {
    // Subscribe to what exists now; the port list is the source of truth and
    // SND_SEQ_EVENT_PORT_START brings later arrivals.
    rebuildSubscriptions();
    std::vector<pollfd> descriptors(8);
    while(!isInterruptionRequested()){
        receiveEvents();
        const int count=snd_seq_poll_descriptors(sequence,descriptors.data(),int(descriptors.size()),POLLIN);
        if(count<=0){
            setFailure(QStringLiteral("the ALSA sequencer stopped reporting descriptors"));
            return;
        }
        // A bounded wait rather than a blocking read: interruption is checked at
        // least every 100 ms, so close() never hangs on an idle device.
        const int ready=::poll(descriptors.data(),nfds_t(count),100);
        if(ready<0&&errno!=EINTR){
            setFailure(QStringLiteral("waiting for MIDI failed: %1").arg(QString::fromUtf8(std::strerror(errno))));
            return;
        }
    }
}

void MidiInput::receiveEvents() {
    for(;;){
        snd_seq_event_t* event=nullptr;
        // Non-blocking: -EAGAIN means the queue is empty, which is the normal exit
        // from this loop. Any other negative value is treated the same way and the
        // outer loop retries after a poll, so a transient error cannot spin.
        if(snd_seq_event_input(sequence,&event)<0||!event)return;
        handleEvent(*event);
        snd_seq_free_event(event);
    }
}

void MidiInput::handleEvent(const snd_seq_event& event) {
    switch(event.type){
    case SND_SEQ_EVENT_CLIENT_START:
    case SND_SEQ_EVENT_CLIENT_EXIT:
    case SND_SEQ_EVENT_PORT_START:
    case SND_SEQ_EVENT_PORT_EXIT:
        // Hotplug: rebuild rather than diff. Subscriptions to a vanished port are
        // dropped by the sequencer itself, and the set keeps the ones already made.
        rebuildSubscriptions();
        return;
    case SND_SEQ_EVENT_CONTROLLER:
        deliver(MidiBinding::ControlChange,event.data.control.channel,
                event.data.control.param,event.data.control.value);
        return;
    case SND_SEQ_EVENT_NOTEON:
        deliver(MidiBinding::NoteOn,event.data.note.channel,
                event.data.note.note,event.data.note.velocity);
        return;
    default:
        // Pitch bend, aftertouch, clock, program change: the model has no place to
        // put them yet, and inventing a mapping here would be the wrong layer.
        return;
    }
}

void MidiInput::rebuildSubscriptions() {
    snd_seq_client_info_t* clientInfo; snd_seq_client_info_alloca(&clientInfo);
    snd_seq_client_info_set_client(clientInfo,-1);
    const int self=clientIdValue;
    while(snd_seq_query_next_client(sequence,clientInfo)>=0){
        const int client=snd_seq_client_info_get_client(clientInfo);
        // Never subscribe to ourselves; and skip the system client, whose Announce
        // port would echo our own port's announcements back as input.
        if(client==self||client==SND_SEQ_CLIENT_SYSTEM)continue;
        snd_seq_port_info_t* portInfo; snd_seq_port_info_alloca(&portInfo);
        snd_seq_port_info_set_client(portInfo,client);
        snd_seq_port_info_set_port(portInfo,-1);
        while(snd_seq_query_next_port(sequence,portInfo)>=0){
            const int source=snd_seq_port_info_get_port(portInfo);
            const unsigned capabilities=snd_seq_port_info_get_capability(portInfo);
            // A source: readable and subscribable-from. A port that can only be a
            // destination can never deliver to us, so subscribing is just noise.
            if((capabilities&SND_SEQ_PORT_CAP_READ)==0){
                continue;
            }
            if((capabilities&SND_SEQ_PORT_CAP_SUBS_READ)==0){
                continue;
            }
            if(!subscribed.insert({client,source}).second)continue;
            // A failure is not fatal: a port can refuse a subscriber, the sender may
            // already have subscribed to us, and either way the next ports-changed
            // event retries. Forgetting the pair is what makes that retry possible.
            if(snd_seq_connect_from(sequence,portIdValue,client,source)<0){
                subscribed.erase({client,source});
            }
        }
    }
}

void MidiInput::deliver(MidiBinding::Message message,int channel,int number,int value) {
    QList<MidiAction> actions;
    ActionHandler hook;
    {
        QMutexLocker lock(&mutex);
        actions=mapMidiEvent(bindings,message,channel,number,value);
        // No match is the common case — a device transmits every message on its
        // way past — so it queues nothing and calls nobody.
        if(actions.isEmpty())return;
        // The queue is how an application polls, the hook is how it is told now.
        // Both see the same batch; the hook borrows it and must not keep it.
        pending.append(actions);
        hook=handler;
    }
    if(hook)hook(actions);
}
