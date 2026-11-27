#include "control_commands.h"
#include "paths.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QSaveFile>
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QTextStream>
#include <algorithm>
#include <cmath>

static bool parseTarget(const QString& text,ControlCommand& c){
    const QString t=text.toLower();bool ok=false;
    if(t.size()==4&&t.startsWith("mic")){const int n=t.mid(3).toInt(&ok);if(ok&&n>=1&&n<=5){c.kind=ControlCommand::Mic;c.index=n-1;return true;}}
    const QStringList inputs{"system","music","aux"};
    if(inputs.contains(t)){c.kind=ControlCommand::Input;c.index=inputs.indexOf(t);return true;}
    if(t.size()==2&&(t[0]=='a'||t[0]=='b')){const int n=t.mid(1).toInt(&ok);
        if(ok&&t[0]=='a'&&n>=1&&n<=5){c.kind=ControlCommand::Bus;c.index=n-1;return true;}
        if(ok&&t[0]=='b'&&n>=1&&n<=3){c.kind=ControlCommand::Bus;c.index=4+n;return true;}}
    return false;
}

bool parseControlCommand(const QStringList& words,ControlCommand& c,QString& error){
    if(words.size()<2){error="usage: --ctl <mute|gain|bypass|denoise|macro> <target|macro name> [value]";return false;}
    const QString verb=words[0].toLower();
    // A macro name may contain spaces, so the whole tail is the name; parseTarget
    // never sees these words. The queue joins them back with single spaces.
    if(verb=="macro"){
        const QString name=words.mid(1).join(' ').trimmed();
        if(name.isEmpty()){error="macro needs a name";return false;}
        c.verb=ControlCommand::Macro;c.name=name;return true;
    }
    if(verb=="mute")c.verb=ControlCommand::Mute;else if(verb=="gain")c.verb=ControlCommand::Gain;
    else if(verb=="bypass")c.verb=ControlCommand::Bypass;else if(verb=="denoise")c.verb=ControlCommand::Denoise;
    else{error="unknown command '"+words[0]+"'";return false;}
    if(!parseTarget(words[1],c)){error="unknown target '"+words[1]+"' (mic1-5, system, music, aux, a1-a5, b1-b3)";return false;}
    if((c.verb==ControlCommand::Bypass||c.verb==ControlCommand::Denoise)&&c.kind!=ControlCommand::Mic){error=verb+" applies only to mic1-5";return false;}
    if(words.size()>3){error="too many arguments";return false;}
    if(c.verb==ControlCommand::Gain){
        bool ok=false;const double value=words.value(2).toDouble(&ok);
        if(!ok||!std::isfinite(value)){error="gain needs a number in dB";return false;}
        c.db=std::clamp(value,-60.,12.);return true;
    }
    c.mode=ControlCommand::Toggle;
    if(words.size()==3){const QString m=words[2].toLower();
        if(m=="on")c.mode=ControlCommand::On;else if(m=="off")c.mode=ControlCommand::Off;else if(m=="toggle")c.mode=ControlCommand::Toggle;
        else{error="expected on, off or toggle";return false;}}
    return true;
}

static void setFlag(bool& flag,ControlCommand::Mode mode){flag=mode==ControlCommand::Toggle?!flag:mode==ControlCommand::On;}

bool applyControlCommand(Settings& s,const ControlCommand& c,QString& error){
    using C=ControlCommand;
    // A macro is resolved here so the queue applies it exactly like any other
    // command: runMacro writes to a copy and only replaces the settings when
    // every step succeeds, so a failed macro changes nothing and the caller
    // still issues one engine.apply() for the whole drain.
    if(c.verb==C::Macro){
        const Macro* found=nullptr;
        for(const auto& macro:s.macros.macros)
            if(QString::compare(macro.name,c.name,Qt::CaseInsensitive)==0){found=&macro;break;}
        if(!found){error="unknown macro '"+c.name+"'";return false;}
        QString runError;
        if(!runMacro(*found,s,&runError)){error=runError;return false;}
        return true;
    }
    if(c.kind==C::Mic){
        auto& hw=s.hardware(c.index);
        switch(c.verb){case C::Mute:setFlag(hw.mute,c.mode);break;case C::Gain:hw.gain=c.db;break;
            case C::Bypass:setFlag(hw.bypass,c.mode);break;case C::Denoise:setFlag(hw.denoise,c.mode);break;
            case C::Macro:break;}
        return true;
    }
    if(c.kind==C::Input){
        if(c.verb==C::Mute)setFlag(s.routing.inputMute.at(c.index),c.mode);else s.routing.inputGain.at(c.index)=c.db;
        return true;
    }
    if(c.verb==C::Mute)setFlag(s.routing.busMute.at(c.index),c.mode);else s.routing.busGain.at(c.index)=c.db;
    (void)error;return true;
}

// Checks a macro name against the saved settings, which is where macros live
// (settings.json in stateRoot). A missing or unreadable file means no macro is
// saved yet, so the name cannot be known and the CLI refuses it; the controller
// never sees an unknown name this way and reports it in its status area if a
// stale command file still turns up.
static bool macroSaved(const QString& name){
    QFile file(stateRoot()+"/settings.json");
    Settings saved;
    if(file.open(QIODevice::ReadOnly))saved.load(QJsonDocument::fromJson(file.readAll()).object());
    for(const auto& macro:saved.macros.macros)
        if(QString::compare(macro.name,name,Qt::CaseInsensitive)==0)return true;
    return false;
}

int sendControlCommand(const QStringList& words,bool running,const QString& dir){
    QTextStream err(stderr);ControlCommand c;QString error;
    if(!parseControlCommand(words,c,error)){err<<"linux-mixer --ctl: "<<error<<"\n";return 2;}
    if(c.verb==ControlCommand::Macro&&!macroSaved(c.name)){err<<"linux-mixer --ctl: unknown macro '"<<c.name<<"'\n";return 2;}
    if(!running){err<<"linux-mixer --ctl: no running mixer to control\n";return 1;}
    const QString folder=dir+"/ctl";QDir().mkpath(folder);
    static int sequence=0;
    const QString name=QString("%1-%2-%3.cmd").arg(QDateTime::currentMSecsSinceEpoch(),15,10,QChar('0')).arg(QCoreApplication::applicationPid()).arg(++sequence,6,10,QChar('0'));
    QSaveFile file(folder+"/"+name);
    if(!file.open(QIODevice::WriteOnly)||file.write(words.join(' ').toUtf8())<0||!file.commit()){err<<"linux-mixer --ctl: could not queue command\n";return 1;}
    return 0;
}

int drainControlCommands(const QString& dir,Settings& settings,QStringList* errors){
    QDir folder(dir+"/ctl");int applied=0;
    for(const auto& name:folder.entryList({"*.cmd"},QDir::Files,QDir::Name)){
        QFile file(folder.filePath(name));if(!file.open(QIODevice::ReadOnly))continue;
        const QByteArray text=file.read(512);file.close();folder.remove(name);
        ControlCommand c;QString error;
        if(parseControlCommand(QString::fromUtf8(text).split(' ',Qt::SkipEmptyParts),c,error)&&applyControlCommand(settings,c,error))++applied;
        else if(errors)errors->append(error);
    }
    return applied;
}
