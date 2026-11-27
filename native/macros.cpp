#include "macros.h"
#include <QtCore/QJsonValue>

static QString verbText(ControlCommand::Verb verb) {
    switch(verb){
    case ControlCommand::Mute: return "mute";
    case ControlCommand::Gain: return "gain";
    case ControlCommand::Bypass: return "bypass";
    case ControlCommand::Denoise: return "denoise";
    }
    return QString();
}

static QString targetText(const ControlCommand& c) {
    if(c.kind==ControlCommand::Mic)
        return c.index>=0&&c.index<5?QString("mic%1").arg(c.index+1):QString();
    if(c.kind==ControlCommand::Input)
        return QStringList{"system","music","aux"}.value(c.index);
    if(c.index>=0&&c.index<5)return QString("a%1").arg(c.index+1);
    if(c.index>=5&&c.index<8)return QString("b%1").arg(c.index-4);
    return QString();
}

QString macroStepText(const ControlCommand& c) {
    const QString verb=verbText(c.verb),target=targetText(c);
    if(verb.isEmpty()||target.isEmpty())return QString();
    QString text=verb+' '+target;
    if(c.verb==ControlCommand::Gain)text+=' '+QString::number(c.db,'g',10);
    else if(c.mode==ControlCommand::On)text+=" on";
    else if(c.mode==ControlCommand::Off)text+=" off";
    return text;
}

bool parseMacroStep(const QString& text,ControlCommand& step,QString& error) {
    ControlCommand candidate;
    if(!parseControlCommand(text.simplified().split(' ',Qt::SkipEmptyParts),candidate,error))return false;
    step=candidate;return true;
}

CommandScope commandScope(ControlCommand::Verb verb) {
    switch(verb){
    case ControlCommand::Mute:
    case ControlCommand::Gain:
    case ControlCommand::Bypass:
    case ControlCommand::Denoise:
        return CommandScope::Parameter;
    }
    // A verb that nobody classified above is refused rather than assumed safe.
    return CommandScope::GraphRebuild;
}

// Only mute, bypass and denoise carry an on/off/toggle mode; gain leaves Mode at
// its default value, so it must not be read as a toggle step.
static bool hasModeSwitch(ControlCommand::Verb verb) {
    return verb==ControlCommand::Mute||verb==ControlCommand::Bypass||verb==ControlCommand::Denoise;
}

bool macroContainsToggle(const Macro& macro) {
    for(const auto& step:macro.steps)
        if(hasModeSwitch(step.verb)&&step.mode==ControlCommand::Toggle)return true;
    return false;
}

// Mirrors the targets parseTarget accepts. A ControlCommand can also be built by
// hand at edit time, where applyControlCommand's unchecked indexing would be
// out of range, so existence is checked here rather than assumed from parsing.
static bool targetExists(const ControlCommand& c) {
    switch(c.kind){
    case ControlCommand::Mic: return c.index>=0&&c.index<5;
    case ControlCommand::Input: return c.index>=0&&c.index<3;
    case ControlCommand::Bus: return c.index>=0&&c.index<8;
    }
    return false;
}

// One step of a macro, checked without applying it.
static bool validateStep(const ControlCommand& step,const QString& macroName,int position,QString& error) {
    if(!targetExists(step)){
        error=QString("macro '%1' step %2: target does not exist").arg(macroName).arg(position);
        return false;
    }
    if((step.verb==ControlCommand::Bypass||step.verb==ControlCommand::Denoise)&&step.kind!=ControlCommand::Mic){
        error=QString("macro '%1' step %2: %3 applies only to mic1-5")
                  .arg(macroName).arg(position).arg(verbText(step.verb));
        return false;
    }
    if(commandScope(step.verb)!=CommandScope::Parameter){
        error=QString("macro '%1' step %2: '%3' would rebuild the audio graph and cannot run in a macro")
                  .arg(macroName).arg(position).arg(verbText(step.verb));
        return false;
    }
    // Round-trip through the shared parser so a step is guaranteed to parse as
    // well as to apply; this also rejects non-finite gain values.
    const QString text=macroStepText(step);ControlCommand parsed;QString stepError;
    if(text.isEmpty()){
        error=QString("macro '%1' step %2: unknown verb or target").arg(macroName).arg(position);
        return false;
    }
    if(!parseMacroStep(text,parsed,stepError)){
        error=QString("macro '%1' step %2: %3").arg(macroName).arg(position).arg(stepError);
        return false;
    }
    return true;
}

QJsonObject Macro::json() const {
    QJsonArray list;for(const auto& step:steps)list.append(macroStepText(step));
    return {{"name",name},{"steps",list}};
}

bool Macro::load(const QJsonObject& object,QString& error) {
    Macro candidate;
    candidate.name=object.value("name").toString().trimmed();
    if(candidate.name.isEmpty()){error="macro has no name";return false;}
    const auto value=object.value("steps");
    if(!value.isArray()){error="macro '"+candidate.name+"' has no step list";return false;}
    const auto list=value.toArray();
    if(list.isEmpty()){error="macro '"+candidate.name+"' has no steps";return false;}
    for(int i=0;i<list.size();++i){
        ControlCommand command;QString stepError;
        if(!list.at(i).isString()){error=QString("macro '%1' step %2: step must be a string").arg(candidate.name).arg(i+1);return false;}
        if(!parseMacroStep(list.at(i).toString(),command,stepError)){error=QString("macro '%1' step %2: %3").arg(candidate.name).arg(i+1).arg(stepError);return false;}
        candidate.steps.append(command);
    }
    // Parsing proves the text is well formed; validation additionally proves
    // the parsed steps are a runnable unit (existing targets, macro-safe verbs).
    if(!validateMacro(candidate,QList<Macro>{},error))return false;
    *this=candidate;return true;
}

QJsonArray MacroSettings::json() const {
    QJsonArray result;for(const auto& macro:macros)result.append(macro.json());return result;
}

void MacroSettings::load(const QJsonObject& settings) {
    macros.clear();errors.clear();
    if(!settings.contains("macros"))return;
    const auto value=settings.value("macros");
    if(!value.isArray()){errors.append("macros must be a list");return;}
    int position=0;
    for(const auto& entry:value.toArray()){
        ++position;
        if(!entry.isObject()){errors.append(QString("macro %1 is not an object").arg(position));continue;}
        Macro macro;QString error;
        if(!macro.load(entry.toObject(),error)){errors.append(error);continue;}
        bool duplicate=false;
        for(const auto& existing:macros)if(QString::compare(existing.name,macro.name,Qt::CaseInsensitive)==0){duplicate=true;break;}
        if(duplicate){errors.append("duplicate macro name '"+macro.name+"'");continue;}
        macros.append(macro);
    }
}

bool validateMacro(const Macro& macro,const QList<Macro>& existing,QString& error) {
    if(macro.name.trimmed().isEmpty()){error="macro name is empty";return false;}
    for(const auto& other:existing)
        if(QString::compare(other.name,macro.name,Qt::CaseInsensitive)==0){error="duplicate macro name '"+macro.name+"'";return false;}
    if(macro.steps.isEmpty()){error="macro '"+macro.name+"' has no steps";return false;}
    for(int i=0;i<macro.steps.size();++i)
        if(!validateStep(macro.steps.at(i),macro.name,i+1,error))return false;
    return true;
}
