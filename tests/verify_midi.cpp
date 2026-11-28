// MIDI binding model, per-entry persistence and validation against saved macros.
#include "midi.h"
#include "engine.h"
#include <QtCore/QJsonDocument>
#include <QtCore/QTextStream>
#include <initializer_list>

#define CHECK(x) do{if(!(x)){QTextStream(stderr)<<"FAIL line "<<__LINE__<<": "#x"\n";return 1;}}while(0)

static QJsonObject bindingJson(const QString& id,const QString& message,int channel,int number,const QString& action){
    QJsonObject object;
    object.insert("id",id);object.insert("message",message);object.insert("channel",channel);
    object.insert("number",number);object.insert("action",action);
    return object;
}
static QJsonArray midiArray(std::initializer_list<QJsonObject> entries){
    QJsonArray list;for(const auto& entry:entries)list.append(entry);return list;
}
static QJsonObject settingsWith(const QJsonArray& midi){
    QJsonObject object;object.insert("midi",midi);return object;
}
static QJsonObject macroJson(const QString& name,const QStringList& steps){
    QJsonArray list;for(const auto& step:steps)list.append(step);
    QJsonObject object;object.insert("name",name);object.insert("steps",list);return object;
}
// A settings root carrying both a saved macro and the bindings under test, which
// is the shape the file really has: the two features share one settings.json.
static QJsonObject settingsWith(const QJsonArray& midi,const QJsonArray& macros){
    QJsonObject object=settingsWith(midi);object.insert("macros",macros);return object;
}

int main(){
    // Parsing: a stored binding keeps its id, message class, channel, number and
    // action, and the action is the shared ControlCommand rather than new text.
    Settings configured;
    configured.load(settingsWith(midiArray({
        bindingJson("fader-music","cc",0,7,"mute music on"),
        bindingJson("talk","noteon",-1,60,"gain mic1 -6"),
        bindingJson("session","cc",15,64,"macro Stream")}),
        {macroJson("Stream",{"mute music on","gain mic1 -6"})}));
    CHECK(configured.macros.errors.isEmpty());
    CHECK(configured.macros.macros.size()==1);
    MidiBindings loaded;
    loaded.load(settingsWith(midiArray({
        bindingJson("fader-music","cc",0,7,"mute music on"),
        bindingJson("talk","noteon",-1,60,"gain mic1 -6"),
        bindingJson("session","cc",15,64,"macro Stream")}),
        {macroJson("Stream",{"mute music on","gain mic1 -6"})}));
    CHECK(loaded.errors.isEmpty());
    CHECK(loaded.bindings.size()==3);
    const MidiBinding& fader=loaded.bindings[0];
    CHECK(fader.id=="fader-music");
    CHECK(fader.message==MidiBinding::ControlChange);
    CHECK(fader.channel==0&&fader.number==7);
    CHECK(fader.hasAction);
    CHECK(fader.action.verb==ControlCommand::Mute&&fader.action.kind==ControlCommand::Input);
    CHECK(fader.action.index==1&&fader.action.mode==ControlCommand::On);
    const MidiBinding& talk=loaded.bindings[1];
    CHECK(talk.message==MidiBinding::NoteOn&&talk.channel==-1&&talk.number==60);
    CHECK(talk.action.verb==ControlCommand::Gain&&talk.action.kind==ControlCommand::Mic);
    CHECK(talk.action.index==0&&talk.action.db==-6);
    // A macro action is the ControlCommand's Macro verb, so nothing here repeats
    // the macro lookup the --ctl queue already performs.
    const MidiBinding& session=loaded.bindings[2];
    CHECK(session.action.verb==ControlCommand::Macro&&session.action.name=="Stream");

    // Validation accepts that set against the settings it was stored with.
    QString error="stale";
    CHECK(validateMidiBindings(loaded.bindings,configured,error));
    CHECK(error.isEmpty());
    // A macro name is matched case-insensitively, as macro names are everywhere.
    MidiBinding caseMacro=session;caseMacro.action.name="stream";
    CHECK(validateMidiBinding(caseMacro,QList<MidiBinding>{},configured,error));

    // Persistence: the "midi" array written by json() reloads unchanged, and the
    // settings root it is inserted into keeps its other keys intact.
    QJsonObject saved=configured.json();
    CHECK(saved.contains("macros"));
    saved.insert("midi",loaded.json());
    MidiBindings reloaded;reloaded.load(saved);
    CHECK(reloaded.errors.isEmpty());
    CHECK(reloaded.bindings.size()==3);
    CHECK(reloaded.json()==loaded.json());
    CHECK(reloaded.bindings[0].id==fader.id);
    CHECK(reloaded.bindings[1].message==MidiBinding::NoteOn);
    CHECK(reloaded.bindings[2].action.verb==ControlCommand::Macro);
    CHECK(reloaded.bindings[2].action.name=="Stream");
    // The stored text is the CLI's own spelling, not a second dialect.
    CHECK(reloaded.json()[0].toObject().value("action").toString()=="mute music on");
    // The stored text is canonical: "toggle" is the default mode, so it is written
    // nowhere, the same way a macro step drops it.
    {
        ControlCommand toggle;QString parseError;
        CHECK(parseMidiAction("mute music toggle",toggle,parseError));
        CHECK(midiActionText(toggle)=="mute music");
    }
    CHECK(reloaded.json()[2].toObject().value("action").toString()=="macro Stream");

    // Unknown keys inside a binding are ignored, so a later phase can add a field
    // without an older build refusing the entry.
    QJsonObject annotated=bindingJson("annotated","cc",1,20,"gain aux -3");
    annotated.insert("note","added by a later version");
    annotated.insert("14bit",true);
    MidiBindings withUnknown;withUnknown.load(settingsWith(midiArray({annotated})));
    CHECK(withUnknown.errors.isEmpty());
    CHECK(withUnknown.bindings.size()==1);
    CHECK(withUnknown.bindings[0].action.kind==ControlCommand::Input&&withUnknown.bindings[0].action.db==-3);

    // An absent "midi" key is every settings file written before this feature and
    // loads cleanly; so does an explicit empty list.
    MidiBindings absent;absent.load(QJsonObject{});
    CHECK(absent.bindings.isEmpty()&&absent.errors.isEmpty());
    MidiBindings empty;empty.load(settingsWith(QJsonArray{}));
    CHECK(empty.bindings.isEmpty()&&empty.errors.isEmpty());
    // A "midi" value that is not a list is described once, not entry by entry.
    MidiBindings wrongShape;wrongShape.load(QJsonObject{{"midi","cc1"}});
    CHECK(wrongShape.bindings.isEmpty());
    CHECK(wrongShape.errors.size()==1&&wrongShape.errors.first().contains("must be a list"));

    // One malformed entry is dropped on its own; its well-formed neighbours load.
    MidiBindings mixed;
    mixed.load(settingsWith(midiArray({
        bindingJson("good-one","cc",2,10,"mute aux on"),
        bindingJson("","cc",2,11,"mute aux off"),
        bindingJson("bad-action","cc",2,12,"explode mic1"),
        bindingJson("bad-gain","cc",2,13,"gain mic1"),
        bindingJson("good-two","noteon",3,40,"denoise mic2 on")})));
    CHECK(mixed.bindings.size()==2);
    CHECK(mixed.bindings[0].id=="good-one"&&mixed.bindings[1].id=="good-two");
    CHECK(mixed.errors.size()==3);
    CHECK(mixed.errors[0].contains("has no id"));
    CHECK(mixed.errors[1].contains("bad-action")&&mixed.errors[1].contains("unknown command"));
    // A gain step with no number is refused by the shared parser, not by a second
    // range check here.
    CHECK(mixed.errors[2].contains("bad-gain"));
    // A non-object entry is dropped the same way.
    MidiBindings notObject;
    QJsonArray scalar;scalar.append("cc1");
    notObject.load(settingsWith(scalar));
    CHECK(notObject.bindings.isEmpty());
    CHECK(notObject.errors.size()==1&&notObject.errors.first().contains("not an object"));

    // An action naming a macro that does not exist is structurally sound text, so
    // load keeps it and validation refuses it with the macro's name. Deleting a
    // macro must not make its bindings disappear from the file silently.
    MidiBindings dangles;
    dangles.load(settingsWith(midiArray({bindingJson("talk","noteon",-1,60,"macro Missing")})));
    CHECK(dangles.bindings.size()==1);
    CHECK(dangles.errors.isEmpty());
    error.clear();
    CHECK(!validateMidiBindings(dangles.bindings,configured,error));
    CHECK(error.contains("unknown macro")&&error.contains("Missing"));
    // The same binding is accepted once the macro is saved again.
    Settings recreated;
    recreated.load(settingsWith(QJsonArray{},QJsonArray{macroJson("Missing",{"mute mic1 on"})}));
    CHECK(recreated.macros.macros.size()==1);
    CHECK(validateMidiBindings(dangles.bindings,recreated,error));

    // Duplicate ids are refused as a whole: an id is how a binding is addressed,
    // so neither entry can be preferred without guessing which one the user meant.
    MidiBindings duplicate;
    duplicate.load(settingsWith(midiArray({
        bindingJson("fader","cc",0,7,"gain mic1 -6"),
        bindingJson("fader","cc",0,8,"gain mic1 -9"),
        bindingJson("other","cc",0,9,"mute mic1 on")})));
    CHECK(duplicate.bindings.isEmpty());
    CHECK(duplicate.errors.size()==1);
    CHECK(duplicate.errors.first().contains("duplicate MIDI binding id 'fader'"));
    // The whole-list validator reaches the same conclusion for a hand-built list.
    MidiBinding first=loaded.bindings[0];
    MidiBinding second=first;second.number=8;
    error.clear();
    CHECK(!validateMidiBindings({first,second},configured,error));
    CHECK(error.contains("duplicate MIDI binding id '"+first.id+"'"));

    // Verbs and targets outside the existing vocabulary are refused by validation.
    // Parsing a stored action catches most of it at load; a command built by hand
    // (an editor, or a later learn mode) has to be caught here.
    MidiBinding badTarget=loaded.bindings[1];
    badTarget.action.index=9;
    error.clear();
    CHECK(!validateMidiBinding(badTarget,QList<MidiBinding>{},configured,error));
    CHECK(error.contains("target does not exist"));
    MidiBinding badVerb=loaded.bindings[1];
    badVerb.action.verb=static_cast<ControlCommand::Verb>(99);
    CHECK(!validateMidiBinding(badVerb,QList<MidiBinding>{},configured,error));
    CHECK(!error.isEmpty());
    MidiBinding bypassOnInput=loaded.bindings[0];
    bypassOnInput.action.verb=ControlCommand::Bypass;
    bypassOnInput.action.kind=ControlCommand::Input;
    bypassOnInput.action.index=0;
    CHECK(!validateMidiBinding(bypassOnInput,QList<MidiBinding>{},configured,error));
    CHECK(error.contains("applies only to mic1-5"));
    MidiBinding macroWithoutName=loaded.bindings[2];
    macroWithoutName.action.name=QString();
    CHECK(!validateMidiBinding(macroWithoutName,QList<MidiBinding>{},configured,error));
    CHECK(error.contains("needs a macro name"));
    // An action that was never set is refused instead of defaulting to a command.
    MidiBinding unset=loaded.bindings[0];
    unset.hasAction=false;
    CHECK(!validateMidiBinding(unset,QList<MidiBinding>{},configured,error));
    CHECK(error.contains("has no action"));
    CHECK(unset.json().value("action").toString().isEmpty());

    // Channels and controller/note numbers are bound to their wire ranges: -1..15
    // for a channel (-1 meaning any) and 0..127 for a number. Both are refused at
    // load and by validation.
    const QList<int> badChannels{16,-2,99};
    for(int channel:badChannels){
        MidiBindings out;
        out.load(settingsWith(midiArray({bindingJson("range","cc",channel,7,"mute mic1 toggle")})));
        CHECK(out.bindings.isEmpty());
        CHECK(out.errors.size()==1&&out.errors.first().contains("channel"));
        MidiBinding binding=loaded.bindings[0];binding.channel=channel;
        CHECK(!validateMidiBinding(binding,QList<MidiBinding>{},configured,error));
        CHECK(error.contains("channel"));
    }
    const QList<int> badNumbers{128,-1,500};
    for(int number:badNumbers){
        MidiBindings out;
        out.load(settingsWith(midiArray({bindingJson("range","noteon",0,number,"mute mic1 toggle")})));
        CHECK(out.bindings.isEmpty());
        CHECK(out.errors.size()==1&&out.errors.first().contains("number"));
        MidiBinding binding=loaded.bindings[0];binding.number=number;
        CHECK(!validateMidiBinding(binding,QList<MidiBinding>{},configured,error));
        CHECK(error.contains("number"));
    }
    // A missing channel or number is a hand-edit mistake, not an implicit 0.
    MidiBindings noChannel;
    QJsonObject missingChannel=bindingJson("no-channel","cc",0,7,"mute mic1 toggle");
    missingChannel.remove("channel");
    noChannel.load(settingsWith(midiArray({missingChannel})));
    CHECK(noChannel.bindings.isEmpty()&&noChannel.errors.first().contains("has no channel"));
    MidiBindings noNumber;
    QJsonObject missingNumber=bindingJson("no-number","cc",0,7,"mute mic1 toggle");
    missingNumber.remove("number");
    noNumber.load(settingsWith(midiArray({missingNumber})));
    CHECK(noNumber.bindings.isEmpty());
    CHECK(noNumber.errors.size()==1&&noNumber.errors.first().contains("no controller or note number"));
    // The edges of both ranges are accepted, as is the any-channel binding.
    MidiBindings edges;
    edges.load(settingsWith(midiArray({
        bindingJson("low","cc",-1,0,"mute mic1 toggle"),
        bindingJson("high","cc",15,127,"mute mic5 toggle")})));
    CHECK(edges.errors.isEmpty());
    CHECK(edges.bindings.size()==2);
    CHECK(validateMidiBindings(edges.bindings,configured,error));
    // Message classes are "cc" and "noteon" only; anything else is a refusal.
    MidiBindings badMessage;
    badMessage.load(settingsWith(midiArray({bindingJson("program","program",0,7,"mute mic1 toggle")})));
    CHECK(badMessage.bindings.isEmpty());
    CHECK(badMessage.errors.size()==1&&badMessage.errors.first().contains("cc or noteon"));
    MidiBinding::Message message=MidiBinding::NoteOn;
    CHECK(parseMidiMessage("CC",message,error)&&message==MidiBinding::ControlChange);
    CHECK(midiMessageText(MidiBinding::NoteOn)=="noteon");

    QTextStream(stdout)<<"PASS: MIDI binding parsing, range and vocabulary refusal, per-entry drop, "
                        "whole-collection duplicate-id refusal and settings persistence.\n";
    return 0;
}
