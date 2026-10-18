#include "paths.h"
#include "engine.h"
#include "routing.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QRegularExpression>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <csignal>
#include <unistd.h>

static const QString source="linux_mixer.microphone", capture="linux_mixer.capture";
static const QString processed="linux_mixer.processed", broadcastCapture="linux_mixer.broadcast";
static double bounded(const QJsonObject& o,const char* key,double fallback,double low,double high) {
    const auto v=o.value(key); const double n=v.toDouble(fallback);
    return std::isfinite(n) ? std::clamp(n,low,high) : fallback;
}
QString captureName(int i){return i==0?"linux_mixer.capture":QString("linux_mixer.hardware%1.capture").arg(i+1);}
QString processedName(int i){return i==0?"linux_mixer.processed":QString("linux_mixer.hardware%1.processed").arg(i+1);}
QJsonObject HardwareSettings::json()const {
    return {{"mic",mic},{"channel",channel},{"threshold",threshold},{"attack",attack},{"release",release},
        {"compression",compression},{"colorX",colorX},{"colorY",colorY},{"gain",gain},
        {"denoise",denoise},{"bypass",bypass},{"mute",mute}};
}
void HardwareSettings::load(const QJsonObject& o){
    mic=o.value("mic").toString(mic);channel=o.value("channel").toString(channel);
    threshold=bounded(o,"threshold",threshold,-60,0);
    attack=bounded(o,"attack",attack,.1,500);release=bounded(o,"release",release,.1,500);
    compression=bounded(o,"compression",compression,0,10);
    colorX=bounded(o,"colorX",colorX,0,1);colorY=bounded(o,"colorY",colorY,0,1);gain=bounded(o,"gain",gain,-60,12);
    denoise=o.value("denoise").toBool(denoise);bypass=o.value("bypass").toBool(bypass);mute=o.value("mute").toBool(mute);
}
QJsonObject Settings::json() const {
    auto o=HardwareSettings::json();QJsonArray hardware;for(int i=0;i<5;++i)hardware.append(this->hardware(i).json());
    o.insert("engineEnabled",engineEnabled);o.insert("tape",tape.json());o.insert("version",3);o.insert("hardware",hardware);o.insert("routing",routing.json());o.insert("sink",sink);
    return o;
}
void Settings::load(const QJsonObject& o) {
    tape.load(o.value("tape").toObject());HardwareSettings::load(o);const auto hardware=o.value("hardware").toArray();for(int i=0;i<5&&i<hardware.size();++i)this->hardware(i).load(hardware[i].toObject());
    engineEnabled=o.value("engineEnabled").toBool(engineEnabled);sink=o.value("sink").toString(sink);
    if(o.value("routing").isObject())routing.load(o.value("routing").toObject());
    else {
        routing.outputs[0]=sink;routing.inputGain[2]=bounded(o,"auxGain",0,-60,12);routing.inputMute[2]=o.value("auxMute").toBool();
        routing.busGain[5]=bounded(o,"busGain",0,-60,12);routing.busMute[5]=o.value("busMute").toBool();
        routing.routes[0][5]=o.value("broadcast").toBool(true);routing.routes[7][0]=o.value("auxA1").toBool(true);
    }
    sink=routing.outputs[0];
}
bool Engine::writeJson(const QString& path,const QJsonObject& object) {
    QSaveFile f(path);
    if(!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(object).toJson()); return f.commit();
}
Engine::Engine(QObject* parent):QObject(parent),root(stateRoot()),runtime(root+"/.runtime") {
    QDir().mkpath(runtime);
    QFile saved(root+"/settings.json");
    if(saved.open(QIODevice::ReadOnly)) settings.load(QJsonDocument::fromJson(saved.readAll()).object());
    matrix=std::make_unique<RoutingMatrix>(*this);
    recorder=std::make_unique<Recorder>(settings.tape,[this]{std::array<RecorderSource,5> result;for(int i=0;i<5;++i)result[i]={settings.hardware(i).mic,settings.hardware(i).channel};return result;},this);
    recorder->changed=[this]{notify();};recorder->needGraph=[this]{refresh();};
    connect(&discovery,&QProcess::finished,this,[this](int code,QProcess::ExitStatus){
        if(code!=0) { connected=false; message="PipeWire unavailable: "+QString::fromUtf8(discovery.readAllStandardError()).trimmed(); notify(); return; }
        QJsonParseError error;
        const auto data=QJsonDocument::fromJson(discovery.readAllStandardOutput(),&error);
        if(error.error!=QJsonParseError::NoError || !data.isArray()) return;
        extraCapture.fill(-1);devices.clear(); captureId=-1; sourceId=-1; broadcastId=-1; auxId=-1;bool microphoneCompatible=false;
        for(const auto& item:data.array()) {
            const auto n=item.toObject();
            if(n.value("type").toString()!="PipeWire:Interface:Node") continue;
            const auto info=n.value("info").toObject(); const auto p=info.value("props").toObject();
            const QString name=p.value("node.name").toString(), kind=p.value("media.class").toString();
            if(name==capture){captureId=n.value("id").toInt();microphoneCompatible=p.value("linux.mixer.graph.version").toVariant().toString()=="3";}
            for(int i=1;i<5;++i)if(name==captureName(i)){extraCapture[i-1]=n.value("id").toInt();microphoneCompatible=p.value("linux.mixer.graph.version").toVariant().toString()=="3";}
            if(name==broadcastCapture) broadcastId=n.value("id").toInt();
            if(name==processed) sourceId=n.value("id").toInt();
            if(name=="linux_mixer.aux" && kind=="Audio/Sink")auxId=n.value("id").toInt();
            // Include virtual microphones (soundboards, Bluetooth, etc.), but hide our own buses to avoid feedback.
            if((kind=="Audio/Source" || kind=="Audio/Sink") && !name.startsWith("linux_mixer."))
                {QStringList channels;for(const auto& v:p.value("audio.position").toArray())channels.append(v.toString());
                if(channels.empty()){const auto raw=p.value("audio.position").toString();QString clean=raw;clean.remove(QRegularExpression("[\\[\\]\"]"));channels=clean.split(QRegularExpression("[,\\s]+"),Qt::SkipEmptyParts);}
                devices.push_back({name,p.value("node.description").toString(name),kind=="Audio/Source",channels});}
        }
        connected=true; running=sourceId>=0 || (settings.mic.isEmpty() && ownedPid("engine")>0 && std::all_of(settings.extra.begin(),settings.extra.end(),[](const auto& hw){return hw.mic.isEmpty();})) || std::any_of(extraCapture.begin(),extraCapture.end(),[](int id){return id>=0;}); monitoring=ownedPid("monitor")>0;
        auxRunning=auxId>=0;
        if(auxStarting){
            auxStarting=false;
            if(!auxRunning){stopOwned("aux");auxFailed=true;message="AUX did not create a playback sink; stopped its process";}
        }
        if(settings.mic.isEmpty() && !QFile::exists(root+"/settings.json")) {
            // Prefer a USB microphone when no explicit choice has been saved.
            for(const auto& d:devices) if(d.input && (settings.mic.isEmpty() || d.name.contains("usb-"))) settings.mic=d.name;
        }
        const bool micPresent=std::any_of(devices.begin(),devices.end(),[this](const auto& d){return d.input && d.name==settings.mic;});
        if(starting && running) {starting=false;if(restartRequested){restartRequested=false;QTimer::singleShot(0,this,[this]{start();});} apply(); startMeter(); message="Engine ready · select Linux Mixer Microphone in your voice app";}
        else if(starting) {message="Starting audio engine…";}
        else if(running && !micPresent) {message="Microphone disconnected · waiting for the selected device";}
        else if(running) {message="48 kHz · mono · processing live"; startMeter();}
        else {message="Engine stopped · choose your mic, then Start"; if(meter.state()!=QProcess::NotRunning) meter.terminate();}
        matrix->consume(data.array(),!stoppedExplicitly && (microphoneCompatible || (settings.engineEnabled && ownedPid("engine")>0 && settings.mic.isEmpty() && std::all_of(settings.extra.begin(),settings.extra.end(),[](const auto& hw){return hw.mic.isEmpty();}))));
        if(running&&!microphoneCompatible)message="Restart audio engine to activate the new routing matrix";
        else if(running&&micPresent&&matrix->ready)message="48 kHz · 5 hardware / 3 virtual inputs · routing live";
        if(running && !stoppedExplicitly){settings.engineEnabled=true;recoveryAttempts=0;retryAfter=0;}
        if(!running && settings.engineEnabled && !starting && QDateTime::currentMSecsSinceEpoch()>=retryAfter){
            retryAfter=QDateTime::currentMSecsSinceEpoch()+std::min(30000,2000*(1<<std::min(recoveryAttempts++,4)));
            QTimer::singleShot(0,this,[this]{start();});
        }
        recorder->setActive(settings.engineEnabled&&!stoppedExplicitly);recorder->consume(data.array());notify();
    });
    connect(&discovery,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){connected=false;message="Cannot run pw-dump. Install PipeWire tools.";notify();});
    connect(&control,&QProcess::finished,this,[this](int code,QProcess::ExitStatus){
        if(code!=0) {message="Could not update audio controls: "+QString::fromUtf8(control.readAllStandardError()).trimmed();notify();}
        if(pending) {pending=false; applyTimer.start();}
    });
    connect(&busControl,&QProcess::finished,this,[this](int code,QProcess::ExitStatus){
        if(code!=0){message="Could not update B1 output: "+QString::fromUtf8(busControl.readAllStandardError()).trimmed();notify();}
        if(pending){pending=false;applyTimer.start();}
    });
    connect(&auxControl,&QProcess::finished,this,[this](int code,QProcess::ExitStatus){
        if(code!=0){message="Could not update AUX: "+QString::fromUtf8(auxControl.readAllStandardError()).trimmed();notify();}
        if(pending){pending=false;applyTimer.start();}
    });
    connect(&auxMeter,&QProcess::readyReadStandardOutput,this,[this]{
        auxMeterBuffer+=auxMeter.readAllStandardOutput();const auto bytes=auxMeterBuffer.size()/4*4;float peak=0;
        for(qsizetype i=0;i<bytes;i+=4){float v;std::memcpy(&v,auxMeterBuffer.constData()+i,4);if(std::isfinite(v))peak=std::max(peak,std::abs(v));}
        auxMeterBuffer.remove(0,bytes);auxPeakDb=std::max(20*std::log10(std::max(double(peak),.0000316228)),auxPeakDb-3);notify();
    });
    connect(&auxMeter,&QProcess::finished,this,[this]{auxPeakDb=-90;auxMeterBuffer.clear();notify();});
    connect(&meter,&QProcess::readyReadStandardOutput,this,[this]{
        meterBuffer+=meter.readAllStandardOutput(); const auto bytes=meterBuffer.size()/4*4;
        float peak=0;
        for(qsizetype i=0;i<bytes;i+=4) {float v;std::memcpy(&v,meterBuffer.constData()+i,4);if(std::isfinite(v))peak=std::max(peak,std::abs(v));}
        meterBuffer.remove(0,bytes);
        const double db=20*std::log10(std::max(double(peak),.0000316228));
        peakDb=std::max(db,peakDb-3);hardwarePeak[0]=peakDb;heldDb=std::max(db,heldDb-.25);notify();
    });
    connect(&meter,&QProcess::finished,this,[this]{peakDb=-90;meterBuffer.clear();notify();});
    refreshTimer.setInterval(1800); connect(&refreshTimer,&QTimer::timeout,this,[this]{refresh();});refreshTimer.start();
    applyTimer.setSingleShot(true);applyTimer.setInterval(35);connect(&applyTimer,&QTimer::timeout,this,[this]{sendControls();});
    QTimer::singleShot(0,this,[this]{refresh();});
}
Engine::~Engine() {shutdown();}
void Engine::shutdown(){
    recorder->shutdown();save();refreshTimer.stop();applyTimer.stop();
    for(auto& p:extraControls)if(p&&p->state()!=QProcess::NotRunning)p->waitForFinished(1000);
    for(auto& p:extraMeters)if(p&&p->state()!=QProcess::NotRunning){p->terminate();p->waitForFinished(500);}
    if(control.state()!=QProcess::NotRunning)control.waitForFinished(1000);
    if(busControl.state()!=QProcess::NotRunning)busControl.waitForFinished(1000);
    if(auxControl.state()!=QProcess::NotRunning)auxControl.waitForFinished(1000);
    sendControls();
    if(control.state()!=QProcess::NotRunning)control.waitForFinished(1000);
    if(busControl.state()!=QProcess::NotRunning)busControl.waitForFinished(1000);
    if(auxControl.state()!=QProcess::NotRunning)auxControl.waitForFinished(1000);
    if(meter.state()!=QProcess::NotRunning){meter.terminate();meter.waitForFinished(1000);}
    if(auxMeter.state()!=QProcess::NotRunning){auxMeter.terminate();auxMeter.waitForFinished(1000);}
    if(discovery.state()!=QProcess::NotRunning){discovery.terminate();discovery.waitForFinished(1000);}
    for(auto& p:extraControls)if(p&&p->state()!=QProcess::NotRunning)p->waitForFinished(1000);
    matrix->apply();matrix->shutdown();
}
void Engine::notify(){if(changed)changed();}
void Engine::refresh(){if(discovery.state()==QProcess::NotRunning)discovery.start("pw-dump");}
void Engine::save(){if(!writeJson(root+"/settings.json",settings.json())){message="Could not save settings";notify();}}
qint64 Engine::ownedPid(const QString& kind) const {
    QFile pidFile(runtime+"/"+kind+".pid");if(!pidFile.open(QIODevice::ReadOnly))return 0;
    bool ok=false;const qint64 pid=pidFile.readAll().trimmed().toLongLong(&ok);if(!ok || pid<=1)return 0;
    QFile cmd("/proc/"+QString::number(pid)+"/cmdline");if(!cmd.open(QIODevice::ReadOnly))return 0;
    const auto args=cmd.readAll().split('\0'); if(args.empty())return 0;
    const QString exe=QFileInfo(QString::fromUtf8(args[0])).fileName();
    const bool matches=kind=="engine" ? exe=="pipewire" && args.contains((runtime+"/microphone.conf").toUtf8())
        :exe=="pw-loopback" && args.contains(kind=="aux"?QByteArray("linux_mixer.aux.engine"):QByteArray("linux_mixer.monitor"));
    return matches ? pid : 0;
}
void Engine::stopOwned(const QString& kind){const auto pid=ownedPid(kind);if(pid)kill(pid,SIGTERM);QFile::remove(runtime+"/"+kind+".pid");}

QJsonObject Engine::configuration() const {
    QString gate;
    for(const auto& dir:{"/usr/lib/ladspa/","/usr/lib64/ladspa/","/usr/lib/x86_64-linux-gnu/ladspa/"})
        if(QFile::exists(QString(dir)+"ZamGate-ladspa.so")) {gate=dir;break;}
    auto builtin=[](const char* name,const char* label,QJsonObject controls=QJsonObject{}){
        if(QString(label)=="linear" || QString(label)=="mixer")return QJsonObject{{"type","ladspa"},{"plugin",pluginPath("libmixer_dsp.so")},{"name",name},{"label",QString(label)=="linear"?"smooth_gain":"smooth_mixer8"},{"control",controls}};
        return QJsonObject{{"type","builtin"},{"name",name},{"label",label},{"control",controls}};
    };
    QJsonArray modules{
        QJsonObject{{"name","libpipewire-module-rt"},{"flags",QJsonArray{"ifexists","nofail"}}},
        QJsonObject{{"name","libpipewire-module-protocol-native"}},QJsonObject{{"name","libpipewire-module-client-node"}},QJsonObject{{"name","libpipewire-module-adapter"}}};
    for(int index=0;index<5;++index){
    const auto& hw=settings.hardware(index);if(hw.mic.isEmpty())continue;
    int sourceChannels=1;for(const auto& d:devices)if(d.name==hw.mic)sourceChannels=d.channels.size();
    const double downmix=hw.channel=="MONO"&&sourceChannels>1?.5:1.;
    QJsonArray filters{
        builtin("downmix","linear",{{"Mult",downmix},{"Add",0}}),
        builtin("raw","copy"),
        QJsonObject{{"type","ladspa"},{"name","denoise"},{"plugin",pluginPath("librnnoise_ladspa.so")},
            {"label","noise_suppressor_mono"},{"control",QJsonObject{{"VAD Threshold (%)",0},{"VAD Grace Period (ms)",200},{"Retroactive VAD Grace (ms)",0}}}},
        builtin("clean","mixer",{{"Gain 1",hw.denoise ? 0.:1.},{"Gain 2",hw.denoise ? 1.:0.}}),
        QJsonObject{{"type","ladspa"},{"name","gate"},{"plugin",gate+"ZamGate-ladspa.so"},{"label","ZamGate"},
            {"control",QJsonObject{{"Attack",hw.attack},{"Release",hw.release},{"Threshold",hw.threshold},
                {"Makeup",0},{"Sidechain",0},{"Max gate close",-50},{"Mode open/shut",0}}}},
        builtin("warmth","bq_lowshelf",{{"Freq",180},{"Q",.707},{"Gain",(0.5-hw.colorX)*12}}),
        builtin("presence","bq_peaking",{{"Freq",2800},{"Q",.8},{"Gain",(0.5-hw.colorY)*10}}),
        builtin("air","bq_highshelf",{{"Freq",6500},{"Q",.707},{"Gain",(hw.colorX-.5)*12}}),
        QJsonObject{{"type","ladspa"},{"name","comp"},{"plugin",gate+"ZamComp-ladspa.so"},{"label","ZamComp"},
            {"control",QJsonObject{{"Attack",10},{"Release",120},{"Knee",6},{"Ratio",1+hw.compression*.5},
                {"Threshold",-12-hw.compression*1.8},{"Makeup",0},{"Slew",1},{"Sidechain",0}}}},
        builtin("bypass","mixer",{{"Gain 1",hw.bypass ? 1.:0.},{"Gain 2",hw.bypass ? 0.:1.}}),
        builtin("fader","linear",{{"Mult",hw.mute ? 0.:std::pow(10,hw.gain/20)},{"Add",0}}),
    };
    QJsonArray links;
    auto link=[&links](const char* out,const char* in){links.append(QJsonObject{{"output",out},{"input",in}});};
    link("downmix:Out","raw:In");link("raw:Out","denoise:Input");link("raw:Out","clean:In 1");link("denoise:Output","clean:In 2");
    link("clean:Out","gate:Audio Input 1");link("gate:Audio Output 1","warmth:In");
    link("warmth:Out","presence:In");link("presence:Out","air:In");link("air:Out","comp:Audio Input 1");
    link("raw:Out","bypass:In 1");link("comp:Audio Output 1","bypass:In 2");link("bypass:Out","fader:In");
    QJsonObject args{{"node.description","Linux Mixer Microphone"},{"media.name","Linux Mixer Microphone"},
        {"audio.rate",48000},{"audio.channels",1},{"audio.position",QJsonArray{"MONO"}},
        {"filter.graph",QJsonObject{{"nodes",filters},{"links",links},{"inputs",QJsonArray{"downmix:In"}},{"outputs",QJsonArray{"fader:Out"}}}},
        {"capture.props",QJsonObject{{"node.name",captureName(index)},{"linux.mixer.graph.version",3},{"target.object",hw.mic},{"node.autoconnect",false},{"node.passive",true},{"node.dont-fallback",true},{"audio.rate",48000},{"audio.position",QJsonArray{"MONO"}},{"stream.dont-remix",true}}},
        {"playback.props",QJsonObject{{"node.name",processedName(index)},{"node.description","Linux Mixer processed mic (internal)"},{"media.class","Audio/Source"},
            {"audio.position",QJsonArray{"MONO"}},{"node.virtual",true},{"priority.session",0},{"audio.rate",48000}}}};
    modules.append(QJsonObject{{"name","libpipewire-module-filter-chain"},{"args",args}});
    }
    return {{"context.properties",QJsonObject{{"log.level",2}}},
        {"context.spa-libs",QJsonObject{{"audio.convert.*","audioconvert/libspa-audioconvert"},{"support.*","support/libspa-support"}}},
        {"context.modules",modules}};
}
void Engine::start(){
    if(starting){restartRequested=true;return;}
    if(running && ownedPid("engine")==0){message="Existing mixer graph is not owned by this folder; stop that instance first";notify();return;}
    if(pluginPath("librnnoise_ladspa.so").isEmpty() || pluginPath("libmixer_dsp.so").isEmpty()){message="Audio plugins missing; reinstall or rebuild Linux Mixer";notify();return;}
    bool gatePresent=false;
    for(const auto& dir:{"/usr/lib/ladspa/","/usr/lib64/ladspa/","/usr/lib/x86_64-linux-gnu/ladspa/"})if(QFile::exists(QString(dir)+"ZamGate-ladspa.so")&&QFile::exists(QString(dir)+"ZamComp-ladspa.so"))gatePresent=true;
    if(!gatePresent){message="ZamGate/ZamComp missing: install zam-plugins (Ubuntu) or zam-plugins-ladspa (Arch)";retryAfter=QDateTime::currentMSecsSinceEpoch()+30000;notify();return;}
    stop();stoppedExplicitly=false;settings.engineEnabled=true;starting=true;message="Starting audio engine…";notify();save();
    // Allow the old graph to detach before publishing the same stable node names.
    QTimer::singleShot(400,this,[this]{
        if(!starting)return;
        if(!writeJson(runtime+"/microphone.conf",configuration())){starting=false;message="Could not write engine configuration";notify();return;}
        QProcess launcher;
        launcher.setProgram("pipewire");launcher.setArguments({"-c",runtime+"/microphone.conf"});
        launcher.setStandardOutputFile(runtime+"/engine.log");launcher.setStandardErrorFile(runtime+"/engine.log",QIODevice::Append);
        qint64 pid=0;
        if(!launcher.startDetached(&pid)){starting=false;message="Cannot launch PipeWire engine";notify();return;}
        QFile f(runtime+"/engine.pid");if(f.open(QIODevice::WriteOnly))f.write(QByteArray::number(pid));
        QTimer::singleShot(300,this,[this]{refresh();});
        QTimer::singleShot(5000,this,[this]{if(starting){starting=false;message="Engine failed to publish mic; check .runtime/engine.log";notify();}});
    });
}
void Engine::stop(){
    recorder->setActive(false);settings.engineEnabled=false;stoppedExplicitly=true;restartRequested=false;save();starting=false;stopOwned("monitor");matrix->stop();stopOwned("aux");stopOwned("engine");
    for(auto& p:extraMeters)if(p){p->terminate();p->waitForFinished(500);}
    hardwarePeak.fill(-90);
    if(meter.state()!=QProcess::NotRunning)meter.terminate();
    if(auxMeter.state()!=QProcess::NotRunning)auxMeter.terminate();
    running=auxRunning=false;captureId=sourceId=broadcastId=auxId=-1;peakDb=heldDb=auxPeakDb=-90;message="Engine stopped";notify();
}
void Engine::apply(){save();applyTimer.start();notify();}
void Engine::sendControls(){
    matrix->apply();
    for(int index=0;index<5;++index){
    const auto& hw=settings.hardware(index);const int id=index==0?captureId:extraCapture[index-1];if(id<0)continue;
    if(index>0 && !extraControls[index-1]){extraControls[index-1]=std::make_unique<QProcess>();connect(extraControls[index-1].get(),&QProcess::finished,this,[this]{if(pending){pending=false;applyTimer.start();}});}
    auto& target=index==0?control:*extraControls[index-1];
    if(target.state()!=QProcess::NotRunning){pending=true;continue;}
    QJsonArray parameters;
    auto add=[&](const char* name,double value){parameters.append(name);parameters.append(value);};
    add("gate:Threshold",hw.threshold);add("gate:Attack",hw.attack);add("gate:Release",hw.release);
    add("clean:Gain 1",hw.denoise?0:1);add("clean:Gain 2",hw.denoise?1:0);
    add("warmth:Gain",(0.5-hw.colorX)*12);add("presence:Gain",(0.5-hw.colorY)*10);add("air:Gain",(hw.colorX-.5)*12);
    add("comp:Ratio",1+hw.compression*.5);add("comp:Threshold",-12-hw.compression*1.8);
    add("bypass:Gain 1",hw.bypass?1:0);add("bypass:Gain 2",hw.bypass?0:1);
    add("fader:Mult",hw.mute ? 0:std::pow(10,hw.gain/20));
    target.start("pw-cli",{"set-param",QString::number(id),"Props",QString::fromUtf8(QJsonDocument(QJsonObject{{"params",parameters}}).toJson(QJsonDocument::Compact))});
    }
}

void Engine::ensureAux(bool restart){
    Q_UNUSED(restart);
    matrix->setOutput(0,settings.sink);
    refresh();
}
void Engine::startMeter(){
    for(int i=1;i<5;++i){
        if(extraCapture[i-1]<0)continue;
        auto& ptr=extraMeters[i-1];if(!ptr){ptr=std::make_unique<QProcess>();auto* p=ptr.get();
            connect(p,&QProcess::readyReadStandardOutput,this,[this,p,i]{const auto data=p->readAllStandardOutput();float peak=0;for(qsizetype j=0;j+4<=data.size();j+=4){float v;std::memcpy(&v,data.constData()+j,4);if(std::isfinite(v))peak=std::max(peak,std::abs(v));}hardwarePeak[i]=std::max(20*std::log10(std::max(double(peak),.0000316228)),hardwarePeak[i]-3);notify();});}
        if(ptr->state()==QProcess::NotRunning)ptr->start("pw-record",{"--raw","--target",processedName(i),"--properties","node.dont-fallback=true stream.monitor=true","--rate","48000","--channels","1","--format","f32","--latency","1024","-"});
    }
    if(sourceId<0 || meter.state()!=QProcess::NotRunning)return;
    meter.setReadChannel(QProcess::StandardOutput);meter.start("pw-record",{"--raw","--target",processed,"--properties","node.dont-fallback=true stream.monitor=true","--rate","48000","--channels","1","--format","f32","--latency","1024","-"});
}
void Engine::setMonitor(bool enabled){
    stopOwned("monitor");
    matrix->setRoute(0,0,enabled);monitoring=enabled && matrix->ready;
    notify();
}
void Engine::exportPreset(const QString& path){if(!path.isEmpty()){message=writeJson(path,settings.json())?"Preset saved":"Could not save preset";notify();}}
void Engine::importPreset(const QString& path){
    if(recorder->recording||recorder->finalizing){message="Stop recording before loading a preset";notify();return;}
    QFile f(path);if(!f.open(QIODevice::ReadOnly)){message="Could not read preset";notify();return;}
    QJsonParseError error;auto doc=QJsonDocument::fromJson(f.readAll(),&error);
    if(error.error!=QJsonParseError::NoError || !doc.isObject() || (doc.object().value("version").toInt()!=1 && doc.object().value("version").toInt()!=2 && doc.object().value("version").toInt()!=3)){message="Invalid mixer preset";notify();return;}
    const auto previous=settings;const auto oldMic=settings.mic,oldSink=settings.sink;const auto oldOutputs=settings.routing.outputs;settings.load(doc.object());
    settings.engineEnabled=previous.engineEnabled;
    if(running){for(int i=0;i<5;++i){settings.hardware(i).mic=previous.hardware(i).mic;settings.hardware(i).channel=previous.hardware(i).channel;}settings.mic=oldMic;settings.sink=oldSink;settings.routing.outputs=oldOutputs;message="Preset loaded; current audio devices kept";}
    const bool topologyChanged=previous.routing.busComposite!=settings.routing.busComposite || previous.routing.busCompositePost!=settings.routing.busCompositePost || previous.routing.compositePatch!=settings.routing.compositePatch;
    recorder->configure();matrix->rulesChanged();if(running&&topologyChanged)start();apply();notify();
}
