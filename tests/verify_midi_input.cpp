// MIDI input: the event-to-action mapping, and a real ALSA sequencer round trip
// through the reader when this machine has /dev/snd/seq.
//
// The mapping is tested through the pure functions with no device at all, which is
// the path CI takes. The reader is then tested for real by opening a second
// sequencer client inside this process, subscribing it to the reader's port and
// sending events through the sequencer; the final line of output says which of
// the two paths this run took.
#include "midi_input.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QTextStream>
#include <QtCore/QThread>
#include <alsa/asoundlib.h>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <initializer_list>

#define CHECK(x) do{if(!(x)){QTextStream(stderr)<<"FAIL line "<<__LINE__<<": " #x "\n";return 1;}}while(0)

static MidiBinding makeBinding(const QString& id,MidiBinding::Message message,int channel,
                               int number,const QString& action) {
    MidiBinding binding;
    binding.id=id;binding.message=message;binding.channel=channel;binding.number=number;
    QString error;
    // The model's own parser, so the test builds bindings exactly the way a loaded
    // file does rather than by hand-assembling ControlCommands.
    if(!parseMidiAction(action,binding.action,error)){
        QTextStream(stderr)<<"bad action in test: "<<error<<"\n";
        std::exit(2);
    }
    binding.hasAction=true;
    return binding;
}

static MidiBindings bindingsOf(std::initializer_list<MidiBinding> entries) {
    MidiBindings collection;
    for(const auto& entry:entries)collection.bindings.append(entry);
    return collection;
}

static bool near(double value,double expected) {return std::abs(value-expected)<1e-9;}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);

    // ---- Mapping, with no ALSA device anywhere ------------------------------
    // A CC fader maps onto the whole clamp, and the -6 dB stored in the binding is
    // a default, not the value: every position has to move the fader.
    const MidiBindings fader=bindingsOf({makeBinding("fader",MidiBinding::ControlChange,0,74,"gain mic1 -6")});
    {
        auto low=mapMidiEvent(fader,MidiBinding::ControlChange,0,74,0);
        CHECK(low.size()==1);
        CHECK(low[0].bindingId=="fader");
        CHECK(low[0].command.verb==ControlCommand::Gain);
        CHECK(low[0].command.kind==ControlCommand::Mic&&low[0].command.index==0);
        CHECK(near(low[0].command.db,-60.0));
        auto middle=mapMidiEvent(fader,MidiBinding::ControlChange,0,74,64);
        CHECK(middle.size()==1);
        CHECK(near(middle[0].command.db,-60.0+64.0/127.0*72.0));
        auto high=mapMidiEvent(fader,MidiBinding::ControlChange,0,74,127);
        CHECK(high.size()==1);
        CHECK(near(high[0].command.db,12.0));
        // Every one of the 127 steps lands inside the CLI clamp and never moves
        // backwards, which is the property the whole-range map exists for.
        double previous=-1000;
        for(int value=0;value<=127;++value){
            auto step=mapMidiEvent(fader,MidiBinding::ControlChange,0,74,value);
            CHECK(step.size()==1);
            CHECK(step[0].command.db>=-60.0-1e-9&&step[0].command.db<=12.0+1e-9);
            CHECK(step[0].command.db>=previous);
            previous=step[0].command.db;
            CHECK(near(step[0].command.db,midiCcGainDb(value)));
        }
        CHECK(near(midiCcGainDb(0),-60.0)&&near(midiCcGainDb(127),12.0));
    }

    // A CC bound to a switch keeps the stored verb and target and takes only the
    // mode from the wire: 64 is the threshold, below is off.
    const MidiBindings switches=bindingsOf({
        makeBinding("mute",MidiBinding::ControlChange,0,75,"mute music toggle"),
        makeBinding("denoise",MidiBinding::ControlChange,0,76,"denoise mic2 on")});
    {
        auto below=mapMidiEvent(switches,MidiBinding::ControlChange,0,75,63);
        CHECK(below.size()==1);
        CHECK(below[0].command.verb==ControlCommand::Mute);
        CHECK(below[0].command.kind==ControlCommand::Input&&below[0].command.index==1);
        CHECK(below[0].command.mode==ControlCommand::Off);
        CHECK(mapMidiEvent(switches,MidiBinding::ControlChange,0,75,0).value(0).command.mode==ControlCommand::Off);
        auto at=mapMidiEvent(switches,MidiBinding::ControlChange,0,75,64);
        CHECK(at.size()==1&&at[0].command.mode==ControlCommand::On);
        CHECK(mapMidiEvent(switches,MidiBinding::ControlChange,0,75,65).value(0).command.mode==ControlCommand::On);
        CHECK(mapMidiEvent(switches,MidiBinding::ControlChange,0,75,127).value(0).command.mode==ControlCommand::On);
        // A stored "on" is ignored for a switch: the fader decides, so a binding
        // saved with a mode still follows the wire.
        auto denoise=mapMidiEvent(switches,MidiBinding::ControlChange,0,76,10);
        CHECK(denoise.size()==1);
        CHECK(denoise[0].command.verb==ControlCommand::Denoise);
        CHECK(denoise[0].command.kind==ControlCommand::Mic&&denoise[0].command.index==1);
        CHECK(denoise[0].command.mode==ControlCommand::Off);
        CHECK(mapMidiEvent(switches,MidiBinding::ControlChange,0,76,100).value(0).command.mode==ControlCommand::On);
    }

    // A note-on runs its stored action as parsed, mode included, so "toggle" is a
    // button press; velocity 0 is a note-off and does nothing.
    const MidiBindings buttons=bindingsOf({
        makeBinding("toggle",MidiBinding::NoteOn,-1,60,"mute mic1 toggle"),
        makeBinding("hold",MidiBinding::NoteOn,0,61,"gain mic1 -6")});
    {
        auto pressed=mapMidiEvent(buttons,MidiBinding::NoteOn,5,60,100);
        CHECK(pressed.size()==1);
        CHECK(pressed[0].command.verb==ControlCommand::Mute);
        CHECK(pressed[0].command.mode==ControlCommand::Toggle);
        CHECK(mapMidiEvent(buttons,MidiBinding::NoteOn,5,60,1).value(0).command.mode==ControlCommand::Toggle);
        // Velocity 0 is ignored, not mapped to the "off" side of anything.
        CHECK(mapMidiEvent(buttons,MidiBinding::NoteOn,5,60,0).isEmpty());
        // A note-on gain keeps its stored dB: velocity is not a fader position.
        auto held=mapMidiEvent(buttons,MidiBinding::NoteOn,0,61,127);
        CHECK(held.size()==1);
        CHECK(held[0].command.verb==ControlCommand::Gain);
        CHECK(held[0].command.kind==ControlCommand::Mic&&held[0].command.index==0);
        CHECK(near(held[0].command.db,-6.0));
    }

    // Channel -1 matches whatever the device transmits; a bound channel rejects
    // every other channel and still matches its own.
    const MidiBindings channels=bindingsOf({
        makeBinding("any",MidiBinding::ControlChange,-1,80,"mute mic1 on"),
        makeBinding("third",MidiBinding::ControlChange,3,81,"mute mic2 on")});
    {
        CHECK(mapMidiEvent(channels,MidiBinding::ControlChange,0,80,64).size()==1);
        CHECK(mapMidiEvent(channels,MidiBinding::ControlChange,15,80,64).size()==1);
        CHECK(mapMidiEvent(channels,MidiBinding::ControlChange,3,81,64).size()==1);
        CHECK(mapMidiEvent(channels,MidiBinding::ControlChange,4,81,64).isEmpty());
        CHECK(mapMidiEvent(channels,MidiBinding::ControlChange,0,81,64).isEmpty());
    }

    // A macro triggers at the same threshold as a switch and keeps its name.
    const MidiBindings macro=bindingsOf({makeBinding("macro",MidiBinding::ControlChange,-1,64,"macro Stream")});
    {
        CHECK(mapMidiEvent(macro,MidiBinding::ControlChange,9,64,63).isEmpty());
        auto fired=mapMidiEvent(macro,MidiBinding::ControlChange,9,64,64);
        CHECK(fired.size()==1);
        CHECK(fired[0].command.verb==ControlCommand::Macro);
        CHECK(fired[0].command.name=="Stream");
        CHECK(mapMidiEvent(macro,MidiBinding::ControlChange,9,64,127).size()==1);
    }

    // Several bindings on one message all fire, in stored order; this is a normal
    // setup (one knob moving a fader and running a macro), not an ambiguity.
    const MidiBindings several=bindingsOf({
        makeBinding("first",MidiBinding::ControlChange,1,10,"mute mic1 on"),
        makeBinding("second",MidiBinding::ControlChange,-1,10,"gain mic2 -3"),
        makeBinding("other",MidiBinding::ControlChange,1,11,"mute mic2 on")});
    {
        auto all=mapMidiEvent(several,MidiBinding::ControlChange,1,10,64);
        CHECK(all.size()==2);
        CHECK(all[0].bindingId=="first"&&all[1].bindingId=="second");
        CHECK(all[0].command.verb==ControlCommand::Mute);
        CHECK(near(all[1].command.db,-60.0+64.0/127.0*72.0));
    }

    // Nothing matching is normal: an unbound number, an empty list, and a message
    // class that does not match the binding's.
    CHECK(mapMidiEvent(several,MidiBinding::ControlChange,1,99,64).isEmpty());
    CHECK(mapMidiEvent(MidiBindings{},MidiBinding::ControlChange,1,10,64).isEmpty());
    CHECK(mapMidiEvent(buttons,MidiBinding::ControlChange,5,60,64).isEmpty());
    CHECK(mapMidiEvent(several,MidiBinding::NoteOn,1,10,64).isEmpty());
    // A hand-built binding without an action is not a command, so it cannot fire
    // the default "mute mic1".
    {
        MidiBinding unset=makeBinding("unset",MidiBinding::ControlChange,0,82,"mute mic2 on");
        unset.hasAction=false;
        CHECK(mapMidiEvent(bindingsOf({unset}),MidiBinding::ControlChange,0,82,64).isEmpty());
    }
    // Out-of-range wire values are refused, never clamped into a plausible press.
    CHECK(mapMidiEvent(fader,MidiBinding::ControlChange,0,74,128).isEmpty());
    CHECK(mapMidiEvent(fader,MidiBinding::ControlChange,0,74,-1).isEmpty());
    CHECK(mapMidiEvent(fader,MidiBinding::ControlChange,16,74,64).isEmpty());
    CHECK(mapMidiEvent(fader,MidiBinding::ControlChange,0,200,64).isEmpty());

    // ---- The reader, through a real sequencer -------------------------------
    MidiInput input;
    QString error;
    if(!input.open(error)){
        QTextStream(stdout)<<"NOTE: the ALSA sequencer could not be opened ("<<error
            <<"); this run tested the mapping through the pure functions only and did "
              "not exercise the reader.\n";
        QTextStream(stdout)<<"PASS: MIDI event-to-action mapping (gain range, switch "
                             "threshold, note-on, channels, macro, class and zero-match "
                             "cases). Reader path skipped: no ALSA sequencer.\n";
        return 0;
    }
    CHECK(error.isEmpty());
    CHECK(input.clientId()>=0&&input.portId()>=0);
    // A second open is refused rather than replacing the live client under the
    // thread that is using it.
    {
        QString again;
        CHECK(!input.open(again));
        CHECK(again.contains("already open"));
    }

    const MidiBindings live=bindingsOf({
        makeBinding("live-fader",MidiBinding::ControlChange,0,74,"gain mic1 -6"),
        makeBinding("live-switch",MidiBinding::ControlChange,0,75,"mute music toggle"),
        makeBinding("live-button",MidiBinding::NoteOn,-1,60,"mute mic1 toggle"),
        makeBinding("live-macro",MidiBinding::ControlChange,-1,76,"macro Stream"),
        makeBinding("live-other-channel",MidiBinding::ControlChange,3,77,"mute aux toggle")});
    input.setBindings(live);
    QMutex hookMutex;
    QList<MidiAction> hooked;
    input.setHandler([&](const QList<MidiAction>& batch){
        QMutexLocker lock(&hookMutex);
        hooked+=batch;
    });

    snd_seq_t* sender=nullptr;
    CHECK(snd_seq_open(&sender,"default",SND_SEQ_OPEN_OUTPUT,0)>=0);
    CHECK(snd_seq_set_client_name(sender,"Voicemixer MIDI input test")>=0);
    const int senderPort=snd_seq_create_simple_port(sender,"test output",
        SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC|SND_SEQ_PORT_TYPE_APPLICATION);
    CHECK(senderPort>=0);
    // The sender subscribes to the reader's port. The reader may also have found
    // this port by itself and subscribed from its side; the sequencer refuses the
    // duplicate, which is why EBUSY is accepted here and why the assertions below
    // tolerate a repeated delivery instead of counting exact batches.
    {
        const int result=snd_seq_connect_to(sender,senderPort,input.clientId(),input.portId());
        CHECK(result>=0||result==-EBUSY);
    }

    const int readerClient=input.clientId(),readerPort=input.portId();
    auto sendController=[&](int channel,int number,int value){
        snd_seq_event_t event;snd_seq_ev_clear(&event);
        snd_seq_ev_set_source(&event,senderPort);
        snd_seq_ev_set_dest(&event,readerClient,readerPort);
        snd_seq_ev_set_direct(&event);
        snd_seq_ev_set_controller(&event,channel,number,value);
        return snd_seq_event_output_direct(sender,&event)>=0;
    };
    auto sendNoteOn=[&](int channel,int note,int velocity){
        snd_seq_event_t event;snd_seq_ev_clear(&event);
        snd_seq_ev_set_source(&event,senderPort);
        snd_seq_ev_set_dest(&event,readerClient,readerPort);
        snd_seq_ev_set_direct(&event);
        snd_seq_ev_set_noteon(&event,channel,note,velocity);
        return snd_seq_event_output_direct(sender,&event)>=0;
    };
    // Wait for at least one delivery, then keep draining briefly so a duplicate or
    // an unexpected extra would be seen rather than hidden by the first one.
    auto drain=[&](int milliseconds){
        QList<MidiAction> actions;
        QElapsedTimer deadline;deadline.start();
        while(deadline.elapsed()<milliseconds){
            actions+=input.takeActions();
            if(!actions.isEmpty())break;
            QThread::msleep(5);
        }
        if(!actions.isEmpty()){
            QElapsedTimer settle;settle.start();
            while(settle.elapsed()<120){actions+=input.takeActions();QThread::msleep(5);}
        }
        return actions;
    };

    // Bottom of the fader: -60 dB, not the -6 dB the binding stores.
    CHECK(sendController(0,74,0));
    {
        auto got=drain(2000);
        CHECK(!got.isEmpty());
        for(const auto& action:got){
            CHECK(action.bindingId=="live-fader");
            CHECK(action.command.verb==ControlCommand::Gain);
            CHECK(near(action.command.db,-60.0));
        }
    }
    // Top of the fader.
    CHECK(sendController(0,74,127));
    {
        auto got=drain(2000);
        CHECK(!got.isEmpty());
        for(const auto& action:got){
            CHECK(action.bindingId=="live-fader");
            CHECK(near(action.command.db,12.0));
        }
    }
    // The switch follows the wire, one step either side of the threshold.
    CHECK(sendController(0,75,63));
    {
        auto got=drain(2000);
        CHECK(!got.isEmpty());
        for(const auto& action:got){
            CHECK(action.bindingId=="live-switch");
            CHECK(action.command.verb==ControlCommand::Mute);
            CHECK(action.command.mode==ControlCommand::Off);
        }
    }
    CHECK(sendController(0,75,64));
    {
        auto got=drain(2000);
        CHECK(!got.isEmpty());
        for(const auto& action:got){
            CHECK(action.bindingId=="live-switch");
            CHECK(action.command.mode==ControlCommand::On);
        }
    }
    // A note-on on channel 5 matches the any-channel button and keeps "toggle".
    CHECK(sendNoteOn(5,60,100));
    {
        auto got=drain(2000);
        CHECK(!got.isEmpty());
        for(const auto& action:got){
            CHECK(action.bindingId=="live-button");
            CHECK(action.command.verb==ControlCommand::Mute);
            CHECK(action.command.mode==ControlCommand::Toggle);
        }
    }
    // Velocity 0 is a note-off: nothing is delivered, so the queue stays empty.
    CHECK(sendNoteOn(5,60,0));
    CHECK(drain(300).isEmpty());
    // A CC on channel 4 does not reach a binding pinned to channel 3.
    CHECK(sendController(4,77,127));
    CHECK(drain(300).isEmpty());
    // The same controller does reach it on its own channel, which proves the
    // previous silence was the channel rule and not a dead subscription.
    CHECK(sendController(3,77,127));
    {
        auto got=drain(2000);
        CHECK(!got.isEmpty());
        for(const auto& action:got)CHECK(action.bindingId=="live-other-channel");
    }
    // A macro fires at 64, not at 63.
    CHECK(sendController(9,76,63));
    CHECK(drain(300).isEmpty());
    CHECK(sendController(9,76,64));
    {
        auto got=drain(2000);
        CHECK(!got.isEmpty());
        for(const auto& action:got){
            CHECK(action.bindingId=="live-macro");
            CHECK(action.command.verb==ControlCommand::Macro);
            CHECK(action.command.name=="Stream");
        }
    }
    // An unbound controller number produces no result, which is not an error.
    CHECK(sendController(0,99,127));
    CHECK(drain(300).isEmpty());

    // The hook is the delivery path a later phase marshals onto the GUI thread; it
    // must have seen the same actions the queue carried.
    {
        QMutexLocker lock(&hookMutex);
        CHECK(!hooked.isEmpty());
        for(const auto& action:hooked){
            CHECK(!action.bindingId.isEmpty());
            CHECK(action.bindingId.startsWith("live-"));
        }
    }

    input.close();
    CHECK(input.clientId()<0&&input.portId()<0);
    CHECK(input.takeError().isEmpty());
    CHECK(input.takeActions().isEmpty());
    snd_seq_close(sender);

    QTextStream(stdout)<<"PASS: MIDI event-to-action mapping (gain range, switch "
                         "threshold, note-on, channels, macro, class and zero-match "
                         "cases) and a live ALSA sequencer round trip through the "
                         "reader's own client and port.\n";
    return 0;
}
