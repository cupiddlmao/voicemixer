// The MIDI editor and the path its actions take: Save and its refusals, the
// "MIDI enabled" switch against the real reader, and the one-apply batch the
// console applies.
//
// The editor is driven through its own widgets offscreen against a temporary state
// directory, the way the macro editor case does, so saved user settings are never
// touched. The reader half skips itself when this machine has no ALSA sequencer,
// following verify_midi_input.cpp: it tries to open one and says so when it cannot.
//
// The apply path uses applyMidiBatch() directly rather than instantiating Console:
// that function is what the console's queued slot calls, so the loop under test is
// the loop that runs, without a window, a tray icon or a PipeWire session.
#include "midi_ui.h"
#include "midi_input.h"
#include "engine.h"
#include "macros.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTableWidget>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextStream>
#include <QtCore/QThread>
#include <alsa/asoundlib.h>
#include <cerrno>

#define CHECK(x) do{if(!(x)){QTextStream(stderr)<<"FAIL line "<<__LINE__<<": "#x"\n";return 1;}}while(0)

static QByteArray storedFile(const QString& root) {
    QFile file(root+"/settings.json");
    if(!file.open(QIODevice::ReadOnly))return {};
    return file.readAll();
}

static QJsonObject storedSettings(const QString& root) {
    return QJsonDocument::fromJson(storedFile(root)).object();
}

// A macro for the bindings to name, built through the model's own parser.
static bool addMacro(Engine& engine,const QString& name,const QString& step) {
    Macro macro;macro.name=name;
    ControlCommand command;QString error;
    if(!parseMacroStep(step,command,error))return false;
    macro.steps.append(command);
    engine.settings.macros.macros.append(macro);
    return true;
}

static void fillRow(QTableWidget* table,int row,const QString& id,const QString& message,
                    const QString& channel,const QString& number,const QString& action) {
    const QStringList cells{id,message,channel,number,action};
    for(int column=0;column<cells.size();++column)table->item(row,column)->setText(cells.at(column));
}

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    QTextStream out(stdout);

    QTemporaryDir state;
    CHECK(state.isValid());
    qputenv("LINUX_MIXER_STATE_DIR",state.path().toUtf8());
    Engine engine;
    CHECK(engine.settings.midi.bindings.isEmpty());
    CHECK(!engine.settings.midiEnabled);
    CHECK(addMacro(engine,"Stream","gain mic2 -3"));

    // The reader the console would own. Nothing may open it before the switch does.
    MidiInput reader;
    MidiEditor editor(engine,&reader);
    editor.show();
    app.processEvents();

    auto* table=editor.findChild<QTableWidget*>("midiEditorTable");
    auto* enabledBox=editor.findChild<QCheckBox*>("midiEditorEnabled");
    auto* status=editor.findChild<QLabel*>("midiEditorStatus");
    auto* newButton=editor.findChild<QPushButton*>("midiEditorNew");
    auto* deleteButton=editor.findChild<QPushButton*>("midiEditorDelete");
    auto* learnButton=editor.findChild<QPushButton*>("midiEditorLearn");
    auto* saveButton=editor.findChild<QPushButton*>("midiEditorSave");
    CHECK(table&&enabledBox&&status&&newButton&&deleteButton&&learnButton&&saveButton);
    CHECK(table->rowCount()==0);
    CHECK(!enabledBox->isChecked());
    // A disabled reader is the state Learn has to describe instead of waiting.
    CHECK(reader.clientId()<0);

    // ---- Two bindings typed into the rows, then one Save ----------------------
    newButton->click();
    CHECK(table->rowCount()==1);
    fillRow(table,0,"fader","cc","0","74","gain mic1 -6");
    newButton->click();
    CHECK(table->rowCount()==2);
    fillRow(table,1,"stream","cc","any","64","macro Stream");

    int applies=0;const auto previous=engine.changed;
    engine.changed=[&applies,previous]{++applies;if(previous)previous();};
    saveButton->click();
    engine.changed=previous;
    // One Save is one engine apply cycle, like the macro editor.
    CHECK(applies==1);
    CHECK(status->text().contains("Saved"));
    CHECK(engine.settings.midi.bindings.size()==2);
    CHECK(engine.settings.midi.bindings[0].id=="fader");
    CHECK(engine.settings.midi.bindings[0].message==MidiBinding::ControlChange);
    CHECK(engine.settings.midi.bindings[0].channel==0);
    CHECK(engine.settings.midi.bindings[0].number==74);
    CHECK(engine.settings.midi.bindings[0].action.verb==ControlCommand::Gain);
    CHECK(engine.settings.midi.bindings[0].action.db==-6);
    CHECK(engine.settings.midi.bindings[1].channel==-1);
    CHECK(engine.settings.midi.bindings[1].action.verb==ControlCommand::Macro);
    CHECK(engine.settings.midi.bindings[1].action.name=="Stream");

    // The persisted JSON is what the editor wrote, in the model's canonical shape.
    {
        const auto object=storedSettings(state.path());
        const auto midi=object.value("midi").toArray();
        CHECK(midi.size()==2);
        CHECK(midi[0].toObject().value("id").toString()=="fader");
        CHECK(midi[0].toObject().value("message").toString()=="cc");
        CHECK(midi[0].toObject().value("channel").toInt(-2)==0);
        CHECK(midi[0].toObject().value("number").toInt(-2)==74);
        CHECK(midi[0].toObject().value("action").toString()=="gain mic1 -6");
        CHECK(midi[1].toObject().value("id").toString()=="stream");
        CHECK(midi[1].toObject().value("channel").toInt(-2)==-1);
        CHECK(midi[1].toObject().value("number").toInt(-2)==64);
        CHECK(midi[1].toObject().value("action").toString()=="macro Stream");
        CHECK(!object.contains("midiEnabled"));
        // The same file reloads into a fresh Settings, which is what the next start
        // of the application does.
        Settings reloaded;reloaded.load(object);
        CHECK(reloaded.midi.errors.isEmpty());
        CHECK(reloaded.midi.bindings.size()==2);
        CHECK(reloaded.midi.bindings[1].action.name=="Stream");
    }

    // ---- Refusals leave the stored file exactly as it was ---------------------
    const QByteArray accepted=storedFile(state.path());
    newButton->click();
    CHECK(table->rowCount()==3);
    fillRow(table,2,"broken","cc","0","75","macro Missing");
    saveButton->click();
    // A macro deleted after the binding was saved is named, not silently dropped.
    CHECK(status->text().contains("Not saved"));
    CHECK(status->text().contains("unknown macro 'Missing'"));
    CHECK(engine.settings.midi.bindings.size()==2);
    CHECK(storedFile(state.path())==accepted);

    // A channel outside -1..15 is refused with the model's own wording.
    fillRow(table,2,"broken","cc","17","75","mute mic1 on");
    saveButton->click();
    CHECK(status->text().contains("channel 17 is outside -1..15"));
    CHECK(engine.settings.midi.bindings.size()==2);
    CHECK(storedFile(state.path())==accepted);

    // Drop the refused row again so the rest of the run works from two bindings.
    table->setCurrentCell(2,0);
    deleteButton->click();
    CHECK(table->rowCount()==2);

    // ---- The enable switch writes the flag and drives the real reader --------
    // Whether a sequencer exists is discovered the way verify_midi_input.cpp does
    // it: try to open one, and skip the client assertions when that fails.
    bool alsa=false;
    {
        MidiInput probe;QString error;
        alsa=probe.open(error);
        if(alsa)probe.close();
        else out<<"NOTE: no ALSA sequencer ("<<error<<"); the reader assertions are skipped.\n";
    }
    enabledBox->setChecked(true);
    app.processEvents();
    CHECK(engine.settings.midiEnabled);
    CHECK(storedSettings(state.path()).value("midiEnabled").toBool());
    if(alsa)CHECK(reader.clientId()>=0);
    else CHECK(status->text().contains("not started"));

    // ---- Learn captures the next message a controller sends -----------------
    // A second sequencer client plays the controller, the way verify_midi_input
    // does. The hook runs on the reader thread and only marshals, so the row can
    // change only after this thread has processed the queued call.
    if(alsa) {
        table->setCurrentCell(0,0);
        learnButton->click();
        CHECK(status->text().contains("Listening"));
        snd_seq_t* sender=nullptr;
        CHECK(snd_seq_open(&sender,"default",SND_SEQ_OPEN_OUTPUT,0)>=0);
        CHECK(snd_seq_set_client_name(sender,"Voicemixer MIDI editor test")>=0);
        const int senderPort=snd_seq_create_simple_port(sender,"test output",
            SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ,
            SND_SEQ_PORT_TYPE_MIDI_GENERIC|SND_SEQ_PORT_TYPE_APPLICATION);
        CHECK(senderPort>=0);
        const int connected=snd_seq_connect_to(sender,senderPort,reader.clientId(),reader.portId());
        CHECK(connected>=0||connected==-EBUSY);
        snd_seq_event_t event;snd_seq_ev_clear(&event);
        snd_seq_ev_set_source(&event,senderPort);
        snd_seq_ev_set_dest(&event,reader.clientId(),reader.portId());
        snd_seq_ev_set_direct(&event);
        snd_seq_ev_set_controller(&event,3,21,100);
        CHECK(snd_seq_event_output_direct(sender,&event)>=0);
        QElapsedTimer deadline;deadline.start();
        while(deadline.elapsed()<3000&&table->item(0,2)->text()!="3"){
            app.processEvents();
            QThread::msleep(5);
        }
        CHECK(table->item(0,1)->text()=="cc");
        CHECK(table->item(0,2)->text()=="3");
        CHECK(table->item(0,3)->text()=="21");
        // Learn sets the message, not the action the user already chose.
        CHECK(table->item(0,4)->text()=="gain mic1 -6");
        CHECK(status->text().contains("Learned"));
        snd_seq_close(sender);
        // The learned numbers are only stored through Save, like any other edit.
        saveButton->click();
        CHECK(status->text().contains("Saved"));
        const auto midi=storedSettings(state.path()).value("midi").toArray();
        CHECK(midi.size()==2);
        CHECK(midi[0].toObject().value("channel").toInt(-2)==3);
        CHECK(midi[0].toObject().value("number").toInt(-2)==21);
    } else out<<"NOTE: no ALSA sequencer; Learn was not exercised.\n";

    enabledBox->setChecked(false);
    app.processEvents();
    CHECK(!engine.settings.midiEnabled);
    CHECK(reader.clientId()<0);
    CHECK(!storedSettings(state.path()).contains("midiEnabled"));

    // Learn needs a running reader and a selected row; with the reader stopped the
    // status line has to say so rather than wait for a message that cannot come.
    table->setCurrentCell(0,0);
    learnButton->click();
    CHECK(status->text().contains("not running"));

    // ---- The batch the console applies --------------------------------------
    // A whole batch of mapped actions reaches the editor's settings with one graph
    // update: two bindings on one controller, as one knob moving a fader and
    // muting a music input would.
    engine.settings.hardware(0).gain=0;
    engine.settings.routing.inputMute.at(1)=false;
    MidiBindings pair;
    {
        MidiBinding first;first.id="fader";first.message=MidiBinding::ControlChange;
        first.channel=0;first.number=74;first.hasAction=true;
        MidiBinding second;second.id="music";second.message=MidiBinding::ControlChange;
        second.channel=0;second.number=74;second.hasAction=true;
        QString error;
        CHECK(parseMidiAction("gain mic1 -6",first.action,error));
        CHECK(parseMidiAction("mute music on",second.action,error));
        pair.bindings<<first<<second;
    }
    {
        // The switch follows the wire, so the stored "on" is only a default: 127
        // is the on side of the CC and the fader's top is +12 dB.
        const auto actions=mapMidiEvent(pair,MidiBinding::ControlChange,0,74,127);
        CHECK(actions.size()==2);
        applies=0;engine.changed=[&applies,previous]{++applies;if(previous)previous();};
        applyMidiBatch(engine,actions);
        engine.changed=previous;
        CHECK(applies==1);
        CHECK(engine.settings.hardware(0).gain==12);
        CHECK(engine.settings.routing.inputMute.at(1));
    }
    // A macro action resolves by name at the moment it fires and runs through
    // runMacro, so the settings it changes are the same object the console holds.
    {
        engine.settings.hardware(1).gain=0;
        MidiBindings trigger;
        MidiBinding binding;binding.id="macro";binding.message=MidiBinding::ControlChange;
        binding.channel=-1;binding.number=64;binding.hasAction=true;
        QString error;CHECK(parseMidiAction("macro Stream",binding.action,error));
        trigger.bindings<<binding;
        const auto actions=mapMidiEvent(trigger,MidiBinding::ControlChange,9,64,127);
        CHECK(actions.size()==1);
        applies=0;engine.changed=[&applies,previous]{++applies;if(previous)previous();};
        applyMidiBatch(engine,actions);
        engine.changed=previous;
        CHECK(applies==1);
        CHECK(engine.settings.hardware(1).gain==-3);
    }
    // A macro deleted after the binding was saved is skipped on the status line: no
    // dialog, and no graph update for a batch that changed nothing.
    {
        MidiBindings gone;
        MidiBinding binding;binding.id="gone";binding.message=MidiBinding::ControlChange;
        binding.channel=-1;binding.number=65;binding.hasAction=true;
        QString error;CHECK(parseMidiAction("macro Retired",binding.action,error));
        gone.bindings<<binding;
        const auto actions=mapMidiEvent(gone,MidiBinding::ControlChange,0,65,127);
        CHECK(actions.size()==1);
        applies=0;engine.changed=[&applies,previous]{++applies;if(previous)previous();};
        applyMidiBatch(engine,actions);
        engine.changed=previous;
        // One notification, but it is the status-line refresh rather than an
        // apply: the missing macro changed nothing, so the settings still hold the
        // earlier batch's result.
        CHECK(applies==1);
        CHECK(engine.settings.hardware(1).gain==-3);
        CHECK(engine.message.contains("no macro named 'Retired'"));
    }

    if(reader.clientId()>=0)reader.close();
    editor.close();
    out<<"PASS: MIDI editor offscreen (Save, persisted JSON, unknown-macro and bad-channel "
         "refusal with the stored file untouched, the enable switch driving the reader, and "
         "Learn filled from a real sequencer message) and one apply per delivered batch, "
         "including the skipped missing-macro action.\n";
    return 0;
}
