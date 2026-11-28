#include "midi.h"
#include "macros.h"
#include <QtCore/QJsonValue>
#include <cmath>

QString midiMessageText(MidiBinding::Message message) {
    switch(message){
    case MidiBinding::ControlChange: return "cc";
    case MidiBinding::NoteOn: return "noteon";
    }
    return QString();
}

bool parseMidiMessage(const QString& text,MidiBinding::Message& message,QString& error) {
    const QString name=text.trimmed().toLower();
    if(name=="cc"){message=MidiBinding::ControlChange;return true;}
    if(name=="noteon"){message=MidiBinding::NoteOn;return true;}
    error="unknown message '"+text+"' (expected cc or noteon)";
    return false;
}

QString midiActionText(const ControlCommand& action) {
    // A macro action has no target of its own; it is spelled with the verb the
    // --ctl queue already understands, so the saved text and the CLI agree.
    if(action.verb==ControlCommand::Macro){
        const QString name=action.name.trimmed();
        return name.isEmpty()?QString():"macro "+name;
    }
    // Everything else reuses the macro step text, which is the canonical form of
    // exactly the four parameter verbs a binding may use.
    return macroStepText(action);
}

bool parseMidiAction(const QString& text,ControlCommand& action,QString& error) {
    ControlCommand candidate;
    if(!parseControlCommand(text.simplified().split(' ',Qt::SkipEmptyParts),candidate,error))return false;
    action=candidate;return true;
}

// A whole number in a hand-edited file is a value; "cc7" or 7.5 is a mistake, and
// rounding or truncating it would bind a controller the user never named.
static bool wholeNumber(const QJsonValue& value,double& result) {
    if(!value.isDouble())return false;
    result=value.toDouble();
    return std::isfinite(result)&&result==std::floor(result);
}

QJsonObject MidiBinding::json() const {
    // An unset action writes as empty text rather than as the default command, so a
    // hand-built binding can never be saved as "mute mic1 toggle" by accident;
    // load() then refuses it instead of inventing an action.
    return {{"id",id},{"message",midiMessageText(message)},{"channel",channel},
        {"number",number},{"action",hasAction?midiActionText(action):QString()}};
}

bool MidiBinding::load(const QJsonObject& object,QString& error) {
    MidiBinding candidate;
    candidate.id=object.value("id").toString().trimmed();
    if(candidate.id.isEmpty()){error="MIDI binding has no id";return false;}
    if(!object.value("message").isString()){
        error="MIDI binding '"+candidate.id+"' has no message (cc or noteon)";return false;
    }
    QString messageError;
    if(!parseMidiMessage(object.value("message").toString(),candidate.message,messageError)){
        error="MIDI binding '"+candidate.id+"': "+messageError;return false;
    }
    double value=0;
    if(!wholeNumber(object.value("channel"),value)){
        error="MIDI binding '"+candidate.id+"' has no channel (-1 for any, 0-15)";return false;
    }
    if(value<-1||value>15){
        error=QString("MIDI binding '%1': channel %2 is outside -1..15").arg(candidate.id).arg(value);return false;
    }
    candidate.channel=int(value);
    if(!wholeNumber(object.value("number"),value)){
        error="MIDI binding '"+candidate.id+"' has no controller or note number (0-127)";return false;
    }
    if(value<0||value>127){
        error=QString("MIDI binding '%1': controller/note number %2 is outside 0..127").arg(candidate.id).arg(value);return false;
    }
    candidate.number=int(value);
    if(!object.value("action").isString()){
        error="MIDI binding '"+candidate.id+"' has no action";return false;
    }
    const QString text=object.value("action").toString();
    ControlCommand action;QString actionError;
    if(!parseMidiAction(text,action,actionError)){
        error="MIDI binding '"+candidate.id+"': "+actionError;return false;
    }
    candidate.action=action;candidate.hasAction=true;
    *this=candidate;return true;
}

QJsonArray MidiBindings::json() const {
    QJsonArray result;
    for(const auto& binding:bindings)result.append(binding.json());
    return result;
}

void MidiBindings::load(const QJsonObject& settings) {
    bindings.clear();errors.clear();
    if(!settings.contains("midi"))return;
    const auto value=settings.value("midi");
    if(!value.isArray()){errors.append("midi must be a list");return;}
    int position=0;
    for(const auto& entry:value.toArray()){
        ++position;
        if(!entry.isObject()){errors.append(QString("MIDI binding %1 is not an object").arg(position));continue;}
        MidiBinding binding;QString error;
        if(!binding.load(entry.toObject(),error)){errors.append(error);continue;}
        bool duplicate=false;
        for(const auto& existing:bindings)
            if(QString::compare(existing.id,binding.id,Qt::CaseInsensitive)==0){duplicate=true;break;}
        // Unlike a single broken entry, a repeated id cannot be resolved by
        // dropping one of the two: both claim to be the binding the user edits,
        // so keeping either would be a guess. The collection loads empty and the
        // message names the id.
        if(duplicate){
            errors.append("duplicate MIDI binding id '"+binding.id+"'");
            bindings.clear();
            return;
        }
        bindings.append(binding);
    }
}

// Mirrors the targets parseTarget accepts. A ControlCommand can also be built by
// hand — from an editor, or by a later learn mode that fills in a number before it
// knows which target the user picked — so existence is checked here rather than
// assumed from parsing.
static bool targetExists(const ControlCommand& c) {
    switch(c.kind){
    case ControlCommand::Mic: return c.index>=0&&c.index<5;
    case ControlCommand::Input: return c.index>=0&&c.index<3;
    case ControlCommand::Bus: return c.index>=0&&c.index<8;
    }
    return false;
}

static bool macroExists(const Settings& settings,const QString& name) {
    for(const auto& macro:settings.macros.macros)
        if(QString::compare(macro.name,name,Qt::CaseInsensitive)==0)return true;
    return false;
}

// The verb word alone, for messages; midiActionText() already carries the whole
// canonical action text and this is only its first word.
static QString verbWord(const ControlCommand& action) {
    const QString text=midiActionText(action);
    const int space=text.indexOf(' ');
    return space<0?text:text.left(space);
}

bool validateMidiBinding(const MidiBinding& binding,const QList<MidiBinding>& existing,
                         const Settings& settings,QString& error) {
    const QString id=binding.id.trimmed();
    if(id.isEmpty()){error="MIDI binding has no id";return false;}
    for(const auto& other:existing)
        if(QString::compare(other.id,id,Qt::CaseInsensitive)==0){error="duplicate MIDI binding id '"+id+"'";return false;}
    if(binding.channel<-1||binding.channel>15){
        error=QString("MIDI binding '%1': channel %2 is outside -1..15").arg(id).arg(binding.channel);return false;
    }
    if(binding.number<0||binding.number>127){
        error=QString("MIDI binding '%1': controller/note number %2 is outside 0..127").arg(id).arg(binding.number);return false;
    }
    if(!binding.hasAction){error="MIDI binding '"+id+"' has no action";return false;}
    const ControlCommand& action=binding.action;
    // A macro action names a saved macro. The name is resolved here and not at
    // load: bindings and macros share one settings file, and only the whole
    // Settings can say whether a name exists.
    if(action.verb==ControlCommand::Macro){
        const QString name=action.name.trimmed();
        if(name.isEmpty()){error="MIDI binding '"+id+"': a macro action needs a macro name";return false;}
        if(!macroExists(settings,name)){error="MIDI binding '"+id+"': unknown macro '"+name+"'";return false;}
        return true;
    }
    if(!targetExists(action)){error=QString("MIDI binding '%1': target does not exist").arg(id);return false;}
    if((action.verb==ControlCommand::Bypass||action.verb==ControlCommand::Denoise)&&action.kind!=ControlCommand::Mic){
        error=QString("MIDI binding '%1': %2 applies only to mic1-5").arg(id).arg(verbWord(action));return false;
    }
    // A verb nobody classified as a parameter command is refused rather than
    // assumed safe; Macro was handled above, so this catches a hand-built verb
    // outside the four a binding may run.
    if(commandScope(action.verb)!=CommandScope::Parameter){
        error=QString("MIDI binding '%1': '%2' is not a control command a binding may run").arg(id).arg(verbWord(action));return false;
    }
    // Round-trip through the shared parser so a binding is guaranteed to parse as
    // well as to apply; this also rejects a non-finite gain.
    const QString text=midiActionText(action);
    if(text.isEmpty()){error=QString("MIDI binding '%1': unknown verb or target").arg(id);return false;}
    ControlCommand reparsed;QString parseError;
    if(!parseMidiAction(text,reparsed,parseError)){error=QString("MIDI binding '%1': %2").arg(id).arg(parseError);return false;}
    return true;
}

bool validateMidiBindings(const QList<MidiBinding>& bindings,const Settings& settings,QString& error) {
    // Each binding is checked against the ones already accepted, which is where a
    // duplicate id is caught. A duplicate matches the first occurrence, so the
    // refusal names the id rather than a position that a later edit would move.
    QList<MidiBinding> accepted;
    for(const auto& binding:bindings){
        if(!validateMidiBinding(binding,accepted,settings,error))return false;
        accepted.append(binding);
    }
    error.clear();
    return true;
}
