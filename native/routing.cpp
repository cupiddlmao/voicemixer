#include "paths.h"
#include "routing.h"
#include "hardware_links.h"
#include "engine.h"
#include "plugin_racks.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QSaveFile>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#include <QtCore/QDateTime>
#include <QtCore/QFileInfo>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <csignal>

static QString edge(int out,int in){return QString::number(out)+":"+QString::number(in);}
RoutingMatrix::RoutingMatrix(Engine& e):QObject(&e),engine(e),runtime(stateRoot()+"/.runtime"){
    connect(&worker,&QProcess::finished,this,[this](int code,QProcess::ExitStatus){
        const auto error=QString::fromUtf8(worker.readAllStandardError()).trimmed();
        if(code!=0)lastParameters.clear();
        if(code!=0 && !worker.property("quiet").toBool() && !error.contains("File exists")){
            engine.message="Routing command failed: "+error;if(engine.changed)engine.changed();
            const auto args=worker.arguments();
            if(worker.program()=="pactl"&&args.size()>1&&args[0]=="move-sink-input")appliedStreams.remove(streamTokens.value(args[1].toInt()));
        }
        next();
    });
    connect(&worker,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart){lastParameters.clear();engine.message="Missing audio command: "+worker.program();if(engine.changed)engine.changed();QTimer::singleShot(0,this,[this]{next();});}
    });
    connect(&appQuery,&QProcess::finished,this,[this](int code,QProcess::ExitStatus){if(code==0){const auto doc=QJsonDocument::fromJson(appQuery.readAllStandardOutput());if(doc.isArray())parseApps(doc.array());}});
}
RoutingMatrix::~RoutingMatrix(){shutdown();}
qint64 RoutingMatrix::ownedPid()const{
    QFile f(runtime+"/matrix.pid");if(!f.open(QIODevice::ReadOnly))return 0;bool ok=false;const auto pid=f.readAll().trimmed().toLongLong(&ok);if(!ok||pid<=1)return 0;
    QFile cmd(QString("/proc/%1/cmdline").arg(pid));if(!cmd.open(QIODevice::ReadOnly))return 0;const auto args=cmd.readAll().split('\0');
    return !args.empty()&&QFileInfo(QString::fromUtf8(args[0])).fileName()=="pipewire"&&args.contains((runtime+"/matrix.conf").toUtf8())?pid:0;
}
QString RoutingMatrix::nodeForInput(int input)const{return virtualInputName(input);}
void RoutingMatrix::queue(const QString& program,const QStringList& args,bool quiet){
    const QString key=program+'\n'+args.join('\n');if(queued.contains(key))return;
    commands.enqueue({program,args,quiet});queued.insert(key);next();
}
void RoutingMatrix::next(){
    if(worker.state()!=QProcess::NotRunning)return;
    if(commands.empty()){if(dirty){dirty=false;apply();}return;}
    const auto cmd=commands.dequeue();queued.remove(cmd.program+'\n'+cmd.args.join('\n'));worker.setProperty("quiet",cmd.quiet);worker.start(cmd.program,cmd.args);
}
void RoutingMatrix::start(){
    if(starting||stopping||ownedPid()||QDateTime::currentMSecsSinceEpoch()<retryAfter)return;
    failed=false;retryAfter=QDateTime::currentMSecsSinceEpoch()+10000;
    if(nodeIds.contains(virtualInputName(2)) || nodeIds.contains(busOutputName(5))){
        engine.message="Old audio graph detected; restart the mixer engine to upgrade";return;
    }
    QSaveFile config(runtime+"/matrix.conf");
    if(!config.open(QIODevice::WriteOnly)){failed=true;engine.message="Could not write routing graph";return;}
    config.write(QJsonDocument(matrixConfiguration(engine.settings.routing,"linux_mixer",&engine.settings.tape)).toJson());if(!config.commit()){failed=true;return;}
    QProcess p;p.setProgram("pipewire");p.setArguments({"-c",runtime+"/matrix.conf"});p.setStandardOutputFile(runtime+"/matrix.log");p.setStandardErrorFile(runtime+"/matrix.log",QIODevice::Append);
    qint64 pid=0;if(!p.startDetached(&pid)){failed=true;engine.message="Could not launch routing graph";return;}
    QFile f(runtime+"/matrix.pid");if(f.open(QIODevice::WriteOnly))f.write(QByteArray::number(pid));starting=true;
    QTimer::singleShot(500,this,[this]{engine.refresh();});
    QTimer::singleShot(6000,this,[this]{if(starting&&!ready){starting=false;failed=true;engine.message="Routing graph failed; see .runtime/matrix.log";if(engine.changed)engine.changed();}});
}
void RoutingMatrix::consume(const QJsonArray& graph,bool compatible){
    nodeIds.clear();nodeSerials.clear();sinkBySerial.clear();hardwareSinkIds.clear();ports.clear();links.clear();
    for(const auto& v:graph){const auto item=v.toObject(),info=item.value("info").toObject(),p=info.value("props").toObject();const int id=item.value("id").toInt();const QString type=item.value("type").toString();
        if(type=="PipeWire:Interface:Node"){
            const auto name=p.value("node.name").toString();nodeIds.insert(name,id);nodeSerials.insert(name,p.value("object.serial").toInteger());
            if(p.value("media.class").toString()=="Audio/Sink")sinkBySerial.insert(p.value("object.serial").toInt(),name);
            if(p.value("media.class").toString()=="Audio/Sink"&&!name.startsWith("linux_mixer."))hardwareSinkIds.insert(id);
        }else if(type=="PipeWire:Interface:Port"){
            ports.append({id,p.value("node.id").toInt(),p.value("audio.channel").toString(),p.value("port.name").toString(),info.value("direction").toString()});
        }else if(type=="PipeWire:Interface:Link")links.insert(edge(info.value("output-port-id").toInt(),info.value("input-port-id").toInt()));
    }
    if(compatible)for(const auto& pair:hardwareLinks(graph,engine.settings))link(pair.first,pair.second);
    bool versionCompatible=false;for(const auto& v:graph){auto p=v.toObject().value("info").toObject().value("props").toObject();if(p.value("node.name")==busCaptureName(0))versionCompatible=p.value("linux.mixer.matrix.version").toVariant().toString()=="4";}
    const bool previouslyReady=ready;
    ready=versionCompatible;for(int i=0;i<3;++i)ready=ready&&nodeIds.contains(virtualInputName(i));for(int i=0;i<8;++i)ready=ready&&nodeIds.contains(busCaptureName(i))&&nodeIds.contains(busOutputName(i));
    if(compatible && !stopping && !ready)start();
    if(ready){retryAfter=0;starting=false;synchronizeLinks();startMeters();if(!previouslyReady)apply();}
    engine.auxRunning=ready;engine.auxPeakDb=inputPeak[2];engine.monitoring=ready&&engine.settings.routing.routes[0][0];
    refreshApps();
}
int RoutingMatrix::port(const QString& node,const QString& channel,const QString& direction)const{
    const int id=nodeIds.value(node,-1);for(const auto& p:ports)if(p.node==id&&p.direction==direction&&(p.channel==channel||p.name.endsWith("_"+channel)))return p.id;return -1;
}
void RoutingMatrix::link(int out,int in){if(out<0||in<0||links.contains(edge(out,in)))return;queue("pw-link",{QString::number(out),QString::number(in)},true);}
void RoutingMatrix::synchronizeLinks(){
    std::array<bool,8> inserts{};
    // Only switch a strip when both sides of its isolated host are present.
    // A failed/missing host automatically leaves the original dry path intact.
    for(int input=0;input<8;++input){
        const QString host=pluginNodeName(input);
        inserts[input]=engine.plugins->running(input);
        for(const auto& channel:QStringList{"FL","FR"})inserts[input]=inserts[input]&&port(host,channel,"input")>=0&&port(host,channel,"output")>=0;
        if(!inserts[input])continue;
        const QString source=input<5?processedName(input):QString("linux_mixer.input%1.output").arg(input-5);
        for(int c=0;c<2;++c)link(port(source,input<5?"MONO":c==0?"FL":"FR","output"),port(host,c==0?"FL":"FR","input"));
    }
    for(int bus=0;bus<8;++bus){
        for(int input=0;input<8;++input)for(int channel=0;channel<2;++channel){
            QString node=input<5?processedName(input):QString("linux_mixer.input%1.output").arg(input-5);QString channelName=input<5?"MONO":channel==0?"FL":"FR";
            if(engine.settings.routing.busComposite[bus]&&!engine.settings.routing.busCompositePost[bus]){node=input<5?engine.settings.hardware(input).mic:virtualInputName(input-5);channelName=input<5?engine.settings.hardware(input).channel:channel==0?"FL":"FR";if(input<5&&channelName=="MONO"&&port(node,"MONO","output")<0)channelName=channel==0?"FL":"FR";}
            const bool rawComposite=engine.settings.routing.busComposite[bus]&&!engine.settings.routing.busCompositePost[bus];
            if(inserts[input]&&!rawComposite){node=pluginNodeName(input);channelName=channel==0?"FL":"FR";}
            const int output=port(node,channelName,"output");
            const int destination=port(busCaptureName(bus),QString("AUX%1").arg(input*2+channel),"input");
            // Remove the old dry/plugin edge before adding its replacement;
            // mixing both would double the signal and spoil stereo effects.
            for(const auto& existing:ports)if(existing.direction=="output"&&existing.id!=output&&links.contains(edge(existing.id,destination)))queue("pw-link",{"-d",QString::number(existing.id),QString::number(destination)},true);
            link(output,destination);
        }
        if(bus<5){
            const QString target=engine.settings.routing.outputs[bus];
            for(const auto& existing:ports){
                if(existing.node!=nodeIds.value(busOutputName(bus),-1)||existing.direction!="output")continue;
                for(const auto& destination:ports)if(destination.direction=="input"&&hardwareSinkIds.contains(destination.node)&&links.contains(edge(existing.id,destination.id))&&destination.node!=nodeIds.value(target,-1))
                    queue("pw-link",{"-d",QString::number(existing.id),QString::number(destination.id)},true);
            }
            if(!target.isEmpty())for(const auto& c:QStringList{"FL","FR"})link(port(busOutputName(bus),c,"output"),port(target,c,"input"));
        }
        for(const auto& c:QStringList{"FL","FR"})link(port(busOutputName(bus),c,"output"),port(QString("linux_mixer.meter.bus%1").arg(bus),c,"input"));
    }
    for(int input=0;input<3;++input)for(const auto& c:QStringList{"FL","FR"})
        link(port(virtualInputName(input),c,"output"),port(QString("linux_mixer.meter.input%1").arg(input),c,"input"));
}
void RoutingMatrix::apply(){
    if(!ready)return;
    if(worker.state()!=QProcess::NotRunning||!commands.empty()){dirty=true;return;}
    auto set=[this](const QString& node,const QJsonArray& values){const int id=nodeIds.value(node,-1);if(id<0)return;const qint64 serial=nodeSerials.value(node,-1);const auto previous=lastParameters.constFind(node);if(previous!=lastParameters.cend()&&previous->first==serial&&previous->second==values)return;lastParameters.insert(node,{serial,values});queue("pw-cli",{"set-param",QString::number(id),"Props",QString::fromUtf8(QJsonDocument(QJsonObject{{"params",values}}).toJson(QJsonDocument::Compact))});};
    const auto& state=engine.settings.routing;
    for(int i=0;i<3;++i){const double gain=state.inputMute[i]?0:std::pow(10,state.inputGain[i]/20);QJsonArray values{"left:Mult",gain,"right:Mult",gain};for(int c=0;c<2;++c)for(int band=0;band<3;++band){values.append(QString(c==0?"leftEq%1:Gain":"rightEq%1:Gain").arg(band));values.append(state.eq[i][c*3+band]);}set(virtualInputName(i),values);}
    for(int b=0;b<8;++b){QJsonArray values;for(int i=0;i<8;++i){values.append(QString("left:Gain %1").arg(i+1));values.append(state.routes[i][b]?state.panGain(i,0):0.);values.append(QString("right:Gain %1").arg(i+1));values.append(state.routes[i][b]?state.panGain(i,1):0.);}
        const double gain=state.busMute[b]?0:std::pow(10,state.busGain[b]/20);values.append("gainL:Mult");values.append(gain*(1-std::max(0.,state.busBalance[b])));values.append("gainR:Mult");values.append(gain*(1+std::min(0.,state.busBalance[b])));if(state.busComposite[b]){values={};for(int c=0;c<8;++c){values.append(QString("compositeGain%1:Mult").arg(c));values.append(gain);}for(int c=0;c<2;++c){values.append(QString("compositeTape%1:Gain 2").arg(c));values.append(engine.settings.tape.playback[b]?std::pow(10,engine.settings.tape.gain/20):0.);}}
        else {
            for(int effect=0;effect<2;++effect)for(int side=0;side<2;++side)for(int input=0;input<8;++input){values.append(QString(effect==0?"verb%1:Gain %2":"echo%1:Gain %2").arg(side).arg(input+1));values.append(state.routes[input][b]?state.panGain(input,side)*state.fxAmount[input]*(effect==0&&state.reverbEnabled?state.reverbSend[input]:effect==1&&state.delayEnabled?state.delaySend[input]:0.):0.);}
            const std::array<const char*,6> keys{"fx:Room","fx:Damping","fx:Delay ms","fx:Feedback","fx:Reverb return","fx:Delay return"};const std::array<double,6> fx{state.room,state.damping,state.delayMs,state.feedback,state.reverbEnabled?state.reverbReturn[b]:0.,state.delayEnabled?state.delayReturn[b]:0.};for(int k=0;k<6;++k){values.append(keys[k]);values.append(fx[k]);}

            for(int side=0;side<2;++side)for(int channel=0;channel<2;++channel){values.append(QString(side==0?"monoL:Gain %1":"monoR:Gain %1").arg(channel+1));values.append(state.busMono[b]?.5:side==channel?1.:0.);}
            const double tapeGain=engine.settings.tape.playback[b]?std::pow(10,engine.settings.tape.gain/20):0.;values.append("tapeL:Gain 2");values.append(tapeGain);values.append("tapeR:Gain 2");values.append(tapeGain);}
        set(busCaptureName(b),values);}
}
void RoutingMatrix::setRoute(int input,int bus,bool enabled){
    if(input<0||input>7||bus<0||bus>7)return;
    if(enabled&&bus<5&&engine.settings.routing.outputs[bus].isEmpty()){engine.message=QString("Choose a hardware output for A%1 first").arg(bus+1);if(engine.changed)engine.changed();return;}
    engine.settings.routing.routes[input][bus]=enabled;engine.monitoring=engine.settings.routing.routes[0][0];engine.apply();
}
void RoutingMatrix::setOutput(int bus,const QString& target){if(bus<0||bus>=5)return;engine.settings.routing.outputs[bus]=target;if(bus==0)engine.settings.sink=target;engine.save();if(ready)synchronizeLinks();}
void RoutingMatrix::startMeters(){
    for(int i=0;i<11;++i){
        if(meters[i]&&meters[i]->state()!=QProcess::NotRunning)continue;
        if(!meters[i]){
            meters[i]=std::make_unique<QProcess>();auto* p=meters[i].get();
            connect(p,&QProcess::readyReadStandardOutput,this,[this,p,i]{buffers[i]+=p->readAllStandardOutput();const auto bytes=buffers[i].size()/4*4;float peak=0;
                for(qsizetype pos=0;pos<bytes;pos+=4){float v;std::memcpy(&v,buffers[i].constData()+pos,4);if(std::isfinite(v))peak=std::max(peak,std::abs(v));}buffers[i].remove(0,bytes);
                auto& level=i<3?inputPeak[i]:busPeak[i-3];level=std::max(20*std::log10(std::max(double(peak),.0000316228)),level-3);engine.auxPeakDb=inputPeak[2];if(engine.changed)engine.changed();});
            connect(p,&QProcess::finished,this,[this,i]{buffers[i].clear();(i<3?inputPeak[i]:busPeak[i-3])=-90;if(i==2)engine.auxPeakDb=-90;if(engine.changed)engine.changed();});
        }
        // Private, explicitly linked readers avoid desktop policy restoring a stale target.
        const QString name=i<3?QString("linux_mixer.meter.input%1").arg(i):QString("linux_mixer.meter.bus%1").arg(i-3);
        const QString properties="node.autoconnect=false node.dont-fallback=true stream.monitor=true state.restore-target=false state.restore-props=false node.name="+name;
        meters[i]->start("pw-record",{"--raw","--target","0","--properties",properties,"--rate","48000","--channels","2","--format","f32","--latency","1024","-"});
    }
}
void RoutingMatrix::refreshApps(){if(appQuery.state()==QProcess::NotRunning)appQuery.start("pactl",{"-f","json","list","sink-inputs"});}
void RoutingMatrix::rulesChanged(){appliedStreams.clear();refreshApps();}
void RoutingMatrix::parseApps(const QJsonArray& list){
    QHash<QString,PlaybackApp> groups;QSet<QString> current;
    for(const auto& v:list){const auto entry=v.toObject(),p=entry.value("properties").toObject();const auto node=p.value("node.name").toString();
        if(node.startsWith("linux_mixer.")||p.value("application.process.id").toString()==QString::number(QCoreApplication::applicationPid()))continue;
        const QString name=p.value("application.name").toString(p.value("media.name").toString("Audio application"));
        const QString id=p.value("application.id").toString(),binary=p.value("application.process.binary").toString();
        const QString key=!id.isEmpty()?"id:"+id:!binary.isEmpty()?"binary:"+binary:"name:"+name;
        const int index=entry.value("index").toInt();const QString token=QString::number(index)+":"+p.value("object.serial").toString();current.insert(token);streamTokens[index]=token;
        int destination=-1;for(int i=0;i<3;++i)if(sinkBySerial.value(entry.value("sink").toInt())==virtualInputName(i))destination=i;
        auto& app=groups[key];app.key=key;app.name=name;app.streams.append(index);app.input=destination;
        // Preserve streams already assigned to one of our devices during upgrade.
        if(!ready && destination>=0&&!engine.settings.routing.appRules.contains(key)){engine.settings.routing.appRules.insert(key,destination);engine.save();}
        if(ready && engine.settings.routing.appRules.contains(key)&&!appliedStreams.contains(token)){
            const int target=engine.settings.routing.appRules.value(key).toInt(-1);
            if(target>=0&&target<3){if(destination!=target)queue("pactl",{"move-sink-input",QString::number(index),virtualInputName(target)});appliedStreams.insert(token);}
        }
    }
    appliedStreams.intersect(current);apps=groups.values();std::sort(apps.begin(),apps.end(),[](const auto& a,const auto& b){return a.name.toLower()<b.name.toLower();});if(engine.changed)engine.changed();
}
void RoutingMatrix::assignApp(const QString& key,int input){
    if(input<-1||input>=3)return;
    if(input>=0&&!ready){engine.message="Start the routing matrix before assigning apps";if(engine.changed)engine.changed();return;}
    if(input<0)engine.settings.routing.appRules.remove(key);else engine.settings.routing.appRules.insert(key,input);
    for(const auto& app:apps)if(app.key==key)for(const int stream:app.streams){queue("pactl",{"move-sink-input",QString::number(stream),input<0?"@DEFAULT_SINK@":virtualInputName(input)});appliedStreams.insert(streamTokens.value(stream));}
    engine.save();engine.message="App routing saved";QTimer::singleShot(300,this,[this]{refreshApps();});if(engine.changed)engine.changed();
}
void RoutingMatrix::stop(){
    stopping=true;ready=false;starting=failed=false;dirty=false;commands.clear();queued.clear();
    if(worker.state()!=QProcess::NotRunning){worker.terminate();worker.waitForFinished(1000);}
    for(auto& p:meters)if(p&&p->state()!=QProcess::NotRunning){p->terminate();p->waitForFinished(1000);}
    const auto pid=ownedPid();if(pid)kill(pid,SIGTERM);QFile::remove(runtime+"/matrix.pid");nodeIds.clear();ports.clear();links.clear();stopping=false;
}
void RoutingMatrix::shutdown(){
    QElapsedTimer deadline;deadline.start();
    while((!commands.empty()||worker.state()!=QProcess::NotRunning||dirty)&&deadline.elapsed()<2500){next();if(worker.state()!=QProcess::NotRunning)worker.waitForFinished(100);}
    commands.clear();queued.clear();dirty=false;
    if(worker.state()!=QProcess::NotRunning){worker.waitForFinished(1000);if(worker.state()!=QProcess::NotRunning){worker.terminate();worker.waitForFinished(1000);}}
    if(appQuery.state()!=QProcess::NotRunning){appQuery.terminate();appQuery.waitForFinished(1000);}
    for(auto& p:meters)if(p&&p->state()!=QProcess::NotRunning){p->terminate();p->waitForFinished(1000);}
}
