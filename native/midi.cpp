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
    return {{"id",id},{"message",midiMessageText(message)},{"channel",channel},
        {"number",number},{"action",midiActionText(action)}};
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
