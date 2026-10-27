#include "recorder.h"
#include "paths.h"
#include "routing_settings.h"
#include <QtCore/QStandardPaths>
#include <QtCore/QCoreApplication>
#include <QtCore/QJsonDocument>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QDateTime>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <csignal>
#include <unistd.h>
QJsonObject RecorderSettings::json()const{
    QJsonArray a,p;for(bool v:armed)a.append(v);for(bool v:playback)p.append(v);
    return {{"directory",directory},{"prefix",prefix},{"format",format},{"busMode",busMode},{"bus",bus},{"channels",channels},{"bits",bits},{"rate",rate},{"kbps",kbps},{"bufferSeconds",bufferSeconds},{"preSeconds",preSeconds},{"stopAfter",stopAfter},{"gain",gain},{"multitrack",multitrack},{"playOnLoad",playOnLoad},{"loop",loop},{"armed",a},{"playback",p}};
}
void RecorderSettings::load(const QJsonObject& o){
    directory=o.value("directory").toString(directory);prefix=o.value("prefix").toString(prefix);
    const auto f=o.value("format").toString(format);if(QStringList{"WAV","BWF","AIFF","MP3"}.contains(f))format=f;
    busMode=o.value("busMode").toBool(busMode);bus=std::clamp(o.value("bus").toInt(bus),0,7);
    channels=std::clamp(o.value("channels").toInt(channels),1,8);bits=o.value("bits").toInt(bits);if(!QList<int>{8,16,24,32}.contains(bits))bits=16;
    rate=o.value("rate").toInt(rate);if(!QList<int>{8000,11025,16000,22050,24000,32000,44100,48000,88200,96000,176400,192000}.contains(rate))rate=48000;
    kbps=std::clamp(o.value("kbps").toInt(kbps),32,320);bufferSeconds=std::clamp(o.value("bufferSeconds").toInt(bufferSeconds),10,60);
    auto number=[&](const char* key,double old,double lo,double hi){double v=o.value(key).toDouble(old);return std::isfinite(v)?std::clamp(v,lo,hi):old;};
    preSeconds=number("preSeconds",preSeconds,0,bufferSeconds);stopAfter=number("stopAfter",stopAfter,0,604800);gain=number("gain",gain,-60,12);
    multitrack=o.value("multitrack").toBool(multitrack);playOnLoad=o.value("playOnLoad").toBool(playOnLoad);loop=o.value("loop").toBool(loop);
    for(int field=0;field<2;++field){auto a=o.value(field==0?"armed":"playback").toArray();auto& flags=field==0?armed:playback;for(int i=0;i<8&&i<a.size();++i)if(a[i].isBool())flags[i]=a[i].toBool();}
}
QString mediaTool(const QString& name){const QString executable=QStandardPaths::findExecutable(name);if(!executable.isEmpty())return executable;const auto local=QCoreApplication::applicationDirPath()+"/../.deps/ffmpeg/usr/bin/"+name;return QFileInfo::exists(local)?local:QString{};}
Recorder::Recorder(RecorderSettings& settings,std::function<std::array<RecorderSource,5>()> sources,QObject* parent,QString name):QObject(parent),s(settings),physical(std::move(sources)),prefix(std::move(name)){
    if(qEnvironmentVariableIsSet("LINUX_MIXER_DEBUG_TAPE")){int id=0;for(auto* process:{&encoder,&decoder,&player,&probe,&linker}){process->setObjectName(QStringList{"encoder","decoder","player","probe","linker"}[id++]);connect(process,&QProcess::stateChanged,this,[this,process](QProcess::ProcessState state){qInfo("Tape %s state=%d playing=%d paused=%d pending=%d loops=%d pos=%.3f",qPrintable(process->objectName()),int(state),playing,playPaused,decoderPending,completedPlays,position);});}}
    if(s.directory.isEmpty())s.directory=QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/Linux Mixer";
    captureTimer.setInterval(10);connect(&captureTimer,&QTimer::timeout,this,[this]{
        const auto failure=capture.takeError();
        if(!failure.isEmpty()&&!closing){error="Recorder input stopped: "+failure;if(recording){failedRecord=true;stop();}notify();}
        receive();
    });captureTimer.start();
    connect(&encoder,&QProcess::bytesWritten,this,[this]{flushRecording();});
    connect(&encoder,&QProcess::finished,this,[this](int code,QProcess::ExitStatus){finishRecording(code);});
    connect(&encoder,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart){failedRecord=true;error="Cannot launch FFmpeg encoder";recording=finalizing=false;notify();}});
    connect(&probe,&QProcess::finished,this,[this](int code,QProcess::ExitStatus){
        loading=false;if(code){error="Cannot read media: "+QString::fromUtf8(probe.readAllStandardError()).trimmed();file.clear();notify();return;}
        auto o=QJsonDocument::fromJson(probe.readAllStandardOutput()).object();duration=o.value("format").toObject().value("duration").toString().toDouble();
        if(duration<=0){error="This file has no playable audio duration";file.clear();notify();return;}position=0;notify();if(probe.property("autoplay").toBool()&&s.playOnLoad)play();
    });
    connect(&player,&QProcess::started,this,[this]{if(needGraph)QTimer::singleShot(100,this,[this]{needGraph();});});
    connect(&decoder,&QProcess::readyReadStandardOutput,this,[this]{const auto bytes=decoder.readAllStandardOutput();if(player.bytesToWrite()>4*1024*1024){error="Playback output stalled";stopPlayback();notify();return;}player.write(bytes);playedFrames+=bytes.size()/8;});
    connect(&decoder,&QProcess::finished,this,[this](int code,QProcess::ExitStatus){
        if(!playing||closing)return;
        if(code){error="Playback decoder failed: "+QString::fromUtf8(decoder.readAllStandardError()).trimmed();stopPlayback();notify();return;}
        player.closeWriteChannel();
    });
    connect(&player,&QProcess::finished,this,[this](int code,QProcess::ExitStatus){
        if(!playing||closing)return;
        const bool repeat=s.loop&&code==0;++completedPlays;stopPlayback();position=repeat?0:duration;notify();if(repeat)QTimer::singleShot(0,this,[this]{if(!playing&&!closing&&s.loop)play();});
    });
    connect(&linker,&QProcess::finished,this,[this]{nextLink();});
    clockTimer.setInterval(100);connect(&clockTimer,&QTimer::timeout,this,[this]{if(playing&&!playPaused&&!decoderPending){position=std::min(duration,playStart+playClock.elapsed()/1000.);notify();}});clockTimer.start();
}
Recorder::~Recorder(){shutdown();}
void Recorder::notify(){if(changed)changed();}
void Recorder::nextLink(){
    if(closing)return;
    if(linker.state()!=QProcess::NotRunning)return;
    if(linkQueue.empty()){if(decoderPending&&playLinksReady)startDecoder();return;}
    const auto p=linkQueue.dequeue();linker.start("pw-link",{QString::number(p.first),QString::number(p.second)});
}
void Recorder::startCapture(){
    if(capture.state()!=QProcess::NotRunning)return;
    inputReady=false;captureCount=s.busMode?8:16;configuredBus=s.busMode;captureBytes.clear();history.clear();QStringList map;for(int i=0;i<captureCount;++i)map<<QString("AUX%1").arg(i);
    // This is a monitoring consumer, outside the live input/output path. Give
    // its 16-channel pipe enough buffering without forcing a smaller quantum.
    capture.startProcess("pw-record",{"--raw","--target","0","--properties","node.autoconnect=false node.dont-fallback=true node.always-process=true state.restore-props=false node.name="+prefix+".recorder.capture", "--rate","48000","--channels",QString::number(captureCount),"--channel-map",map.join(','),"--format","f32","--latency","2048","-"},captureCount);
    QTimer::singleShot(100,this,[this]{if(!closing&&needGraph)needGraph();});
}
void Recorder::consume(const QJsonArray& graph){
    if(closing||!active)return;
    if(capture.state()==QProcess::NotRunning)startCapture();
    QHash<QString,int> nodes;struct Port{int id,node;QString channel,dir;};QList<Port> ports;QSet<QString> existing;
    for(const auto& v:graph){auto n=v.toObject(),info=n.value("info").toObject(),p=info.value("props").toObject();auto type=n.value("type").toString();
        if(type=="PipeWire:Interface:Node")nodes.insert(p.value("node.name").toString(),n.value("id").toInt());
        if(type=="PipeWire:Interface:Port")ports.append({n.value("id").toInt(),p.value("node.id").toInt(),p.value("audio.channel").toString(),info.value("direction").toString()});
        if(type=="PipeWire:Interface:Link")existing.insert(QString::number(info.value("output-port-id").toInt())+":"+QString::number(info.value("input-port-id").toInt()));
    }
    auto port=[&](const QString& node,const QString& channel,const QString& dir){int id=nodes.value(node,-1);for(auto& p:ports)if(p.node==id&&p.channel==channel&&p.dir==dir)return p.id;return -1;};
    QSet<QString> desired;QList<QPair<int,int>> newLinks;
    auto link=[&](int from,int to){if(from<0||to<0)return;const auto key=QString::number(from)+":"+QString::number(to);desired.insert(key);if(!existing.contains(key))newLinks.append({from,to});};
    const QString target=prefix+".recorder.capture";
    if(s.busMode){const auto bus=busOutputName(s.bus,prefix);const QStringList channels{"FL","FR","FC","LFE","RL","RR","SL","SR"};for(int c=0;c<8;++c)link(port(bus,channels[c],"output"),port(target,QString("AUX%1").arg(c),"input"));}
    else{
        const auto selected=physical();for(int i=0;i<8;++i){if(i<5){const auto& input=selected[i];for(int c=0;c<2;++c){int out=port(input.node,input.channel,"output");if(out<0&&input.channel=="MONO")out=port(input.node,c==0?"FL":"FR","output");link(out,port(target,QString("AUX%1").arg(i*2+c),"input"));}}
            else for(int c=0;c<2;++c)link(port(virtualInputName(i-5,prefix),c==0?"FL":"FR","output"),port(target,QString("AUX%1").arg(i*2+c),"input"));}
    }
    // Remove only obsolete links terminating at this recorder's private capture.
    // Detached pw-link runs without blocking the UI thread; the next refresh
    // re-derives stale links, so a failed or ignored unlink is retried for free.
    for(const auto& out:ports)if(out.dir=="output")for(const auto& in:ports)if(in.node==nodes.value(target,-1)&&in.dir=="input"){
        auto key=QString::number(out.id)+":"+QString::number(in.id);if(existing.contains(key)&&!desired.contains(key)){QProcess::startDetached("pw-link",{"-d",QString::number(out.id),QString::number(in.id)});}}
    int capturePorts=0;for(const auto& in:ports)if(in.node==nodes.value(target,-1)&&in.dir=="input")++capturePorts;
    inputReady=capturePorts==captureCount;for(const auto& edge:desired)inputReady=inputReady&&existing.contains(edge);
    const bool playerExists=nodes.contains(prefix+".recorder.player");
    playLinksReady=playerExists&&nodes.contains(busCaptureName(5,prefix));
    if(playerExists)for(int b=0;b<8;++b)for(int c=0;c<2;++c)link(port(prefix+".recorder.player",c==0?"FL":"FR","output"),port(busCaptureName(b,prefix),QString("AUX%1").arg(16+c),"input"));
    if(linker.state()==QProcess::NotRunning){linkQueue.clear();for(const auto& pair:newLinks)linkQueue.enqueue(pair);nextLink();}
    if(decoderPending&&playerExists&&linkQueue.empty()&&linker.state()==QProcess::NotRunning)startDecoder();
}
void Recorder::setActive(bool value){
    if(active==value)return;
    active=value;if(active){if(needGraph)needGraph();return;}
    stop();inputReady=false;capture.stop();history.clear();captureBytes.clear();linkQueue.clear();
}
void Recorder::configure(){
    if(recording||finalizing){error="Stop recording before changing recording options";notify();return;}
    capture.stop();
    linkQueue.clear();if(linker.state()!=QProcess::NotRunning){linker.kill();linker.waitForFinished(500);}
    error.clear();
    inputReady=false;history.clear();captureBytes.clear();if(needGraph)needGraph();notify();
}
void Recorder::receive(){
    if(capture.takeOverflow()){
        captureBytes.clear();history.clear();
        if(recording){failedRecord=true;error="Recorder could not keep up with a UI stall; recording stopped to prevent silent data loss";stop();notify();}
    }
    captureBytes+=capture.takeAudio();const qsizetype frameBytes=captureCount*4,frames=captureBytes.size()/frameBytes;if(!frames)return;
    const int channels=s.busMode?std::clamp(s.channels,1,8):2;QByteArray audio(frames*channels*4,Qt::Uninitialized);float peakValue=0;
    auto sample=[&](qsizetype frame,int channel){float v;std::memcpy(&v,captureBytes.constData()+(frame*captureCount+channel)*4,4);return std::isfinite(v)?v:0.f;};
    for(qsizetype f=0;f<frames;++f)for(int c=0;c<channels;++c){float v=0;if(s.busMode)v=channels==1?(sample(f,0)+sample(f,1))*.5f:sample(f,c);else for(int i=0;i<8;++i)if(s.armed[i]){if(i<5)v+=(sample(f,i*2)+sample(f,i*2+1))*.5f;else v+=sample(f,i*2+c);}
        peakValue=std::max(peakValue,std::abs(v));quint32 bits;std::memcpy(&bits,&v,4);qToLittleEndian(bits,reinterpret_cast<uchar*>(audio.data()+(f*channels+c)*4));}
    captureBytes.remove(0,frames*frameBytes);peak=std::max(20*std::log10(std::max(double(peakValue),.0000316228)),peak-3);
    // No history is needed when pre-record is off. Keep only the requested
    // duration in a ring when enabled, rather than moving a 20-second buffer
    // on every capture callback on the UI thread.
    history.setCapacity(std::llround(s.preSeconds*48000)*channels*4);history.append(audio);
    if(recording&&!recordPaused)feed(audio);
    if(!meterClock.isValid()||meterClock.elapsed()>=100){meterClock.start();notify();}
}
void Recorder::feed(const QByteArray& audio){
    if(encoder.state()==QProcess::NotRunning||audio.isEmpty())return;
    if(pendingAudio.size()+encoder.bytesToWrite()>qint64(s.bufferSeconds+5)*48000*outputChannels*4){failedRecord=true;error="Recording disk/encoder cannot keep up; stopped to prevent data loss";stop();return;}
    qsizetype bytes=audio.size();if(s.stopAfter>0){const qint64 remaining=std::max<qint64>(0,std::llround(s.stopAfter*48000)-recordedFrames);bytes=std::min<qint64>(bytes,remaining*outputChannels*4);}
    pendingAudio.append(audio.constData(),bytes);flushRecording();recordedFrames+=bytes/(outputChannels*4);position=double(recordedFrames)/48000;
    if(s.stopAfter>0&&position>=s.stopAfter)stop();
}
void Recorder::flushRecording(){
    if(encoder.state()==QProcess::NotRunning)return;
    while(!pendingAudio.isEmpty()&&encoder.bytesToWrite()<262144){const auto size=std::min<qsizetype>(65536,pendingAudio.size());encoder.write(pendingAudio.constData(),size);pendingAudio.remove(0,size);}
    if(finalizing&&pendingAudio.isEmpty())encoder.closeWriteChannel();
}
void Recorder::record(){
    if(recording){recordPaused=!recordPaused;notify();return;}
    if(finalizing)return;
    const auto ffmpeg=mediaTool("ffmpeg");if(ffmpeg.isEmpty()){error="Install FFmpeg to record audio";notify();return;}
    if(capture.state()!=QProcess::Running||!inputReady){error="Recorder source is connecting; try Record when the input meter is ready";notify();return;}
    if(!s.busMode&&std::none_of(s.armed.begin(),s.armed.end(),[](bool a){return a;})){error="Arm at least one input";notify();return;}
    stopPlayback();if(!QDir().mkpath(s.directory)){error="Cannot create recording folder";notify();return;}
    outputChannels=s.busMode?std::clamp(s.channels,1,8):2;if(s.format=="MP3")outputChannels=2;
    if(s.busMode&&outputChannels!=s.channels){error="MP3 requires 2 channels; select stereo in recorder options";notify();return;}
    QString safe=s.prefix.trimmed();safe.replace(QRegularExpression("[/\\\\\\x00-\\x1f]"),"_");if(safe.isEmpty())safe="Record";
    const auto extension=s.format=="MP3"?"mp3":s.format=="AIFF"?"aiff":"wav";
    QString base=QDir(s.directory).filePath(safe+QDateTime::currentDateTime().toString(" yyyy-MM-dd 'at' HH'h'mm'm'ss's'"));
    if(QFileInfo::exists(base+"."+extension))base+=QDateTime::currentDateTime().toString("-zzz");
    recordFile=base+"."+extension;QStringList args{"-hide_banner","-loglevel","error","-n","-f","f32le","-ar","48000","-ac",QString::number(outputChannels),"-i","pipe:0"};
    auto output=[&](const QString& name){QString codec;if(s.format=="MP3")codec="libmp3lame";else if(s.bits==32)codec=s.format=="AIFF"?"pcm_f32be":"pcm_f32le";else if(s.bits==24)codec=s.format=="AIFF"?"pcm_s24be":"pcm_s24le";else if(s.bits==8)codec=s.format=="AIFF"?"pcm_s8":"pcm_u8";else codec=s.format=="AIFF"?"pcm_s16be":"pcm_s16le";
        args<<"-c:a"<<codec<<"-ar"<<QString::number(s.rate);if(s.format=="MP3")args<<"-b:a"<<(QString::number(s.kbps)+"k");if(s.format=="BWF")args<<"-write_bext"<<"1"<<"-metadata"<<"description=Linux Mixer recording";args<<name;};
    args<<"-map"<<"0:a:0";output(recordFile);
    if(s.multitrack)for(int c=0;c<outputChannels;++c){args<<"-map"<<"0:a:0"<<"-af"<<QString("pan=mono|c0=c%1").arg(c);output(base+QString("_Track%1.").arg(c+1)+extension);}
    error.clear();pendingAudio.clear();failedRecord=false;recordedFrames=0;recordPaused=false;recording=true;position=0;
    encoder.start(ffmpeg,args);if(!encoder.waitForStarted(1500)){recording=false;error="Cannot start audio encoder";notify();return;}
    const auto preRoll=history.tail(std::llround(s.preSeconds*48000)*outputChannels*4);if(!preRoll.isEmpty())feed(preRoll);notify();
}
void Recorder::finishRecording(int code){
    if(!recording&&!finalizing)return;
    recording=recordPaused=finalizing=false;if(code||failedRecord){error="Recording failed; check free space and folder permissions. "+QString::fromUtf8(encoder.readAllStandardError()).trimmed();}
    else {file=recordFile;duration=double(recordedFrames)/48000;position=0;}
    notify();
}
void Recorder::loadFile(const QString& path,bool autoplay){
    if(recording||finalizing){error="Stop recording before loading another tape";notify();return;}
    const auto ffprobe=mediaTool("ffprobe");if(ffprobe.isEmpty()){error="Install FFmpeg/ffprobe to load media";notify();return;}
    stopPlayback();if(probe.state()!=QProcess::NotRunning){probe.kill();probe.waitForFinished(500);}
    file=path;duration=position=0;error.clear();loading=true;probe.setProperty("autoplay",autoplay);
    probe.start(ffprobe,{"-v","error","-select_streams","a:0","-show_entries","format=duration","-of","json",file});notify();
}
void Recorder::startDecoder(){
    if(!decoderPending||!playing||player.state()!=QProcess::Running||decoder.state()!=QProcess::NotRunning)return;
    decoderPending=false;playedFrames=0;playClock.restart();
    decoder.start(mediaTool("ffmpeg"),{"-hide_banner","-loglevel","error","-re","-ss",QString::number(playStart,'f',6),"-i",file,"-map","0:a:0","-vn","-f","f32le","-ar","48000","-ac","2","pipe:1"});
}
void Recorder::play(){
    if(recording||finalizing||loading)return;
    if(playing){pause();return;}
    if(file.isEmpty()||duration<=0){error="Load an audio file first";notify();return;}
    if(mediaTool("ffmpeg").isEmpty()){error="Install FFmpeg to play media";notify();return;}
    if(std::none_of(s.playback.begin(),s.playback.end(),[](bool v){return v;})){error="Select a playback bus (A1–A5 or B1–B3)";notify();return;}
    if(position>=duration)position=0;
    playStart=position;playing=true;playPaused=false;decoderPending=true;error.clear();
    player.start("pw-play",{"--raw","--target","0","--properties","node.autoconnect=false node.dont-fallback=true state.restore-props=false state.restore-target=false node.name="+prefix+".recorder.player", "--rate","48000","--channels","2","--format","f32","--latency","512","-"});notify();
}
void Recorder::pause(){
    if(recording){recordPaused=!recordPaused;notify();return;}
    if(!playing)return;
    playPaused=!playPaused;if(decoder.processId()>1)kill(decoder.processId(),playPaused?SIGSTOP:SIGCONT);
    if(!playPaused){playStart=position;playClock.restart();}notify();
}
void Recorder::stopPlayback(){
    playing=playPaused=decoderPending=playLinksReady=false;
    if(decoder.state()!=QProcess::NotRunning){kill(decoder.processId(),SIGCONT);decoder.kill();decoder.waitForFinished(1000);}
    if(player.state()!=QProcess::NotRunning){player.closeWriteChannel();player.terminate();if(!player.waitForFinished(500)){player.kill();player.waitForFinished(1000);}}
}
void Recorder::stop(){
    stopPlayback();if(recording){recording=false;recordPaused=false;finalizing=true;flushRecording();}else if(!finalizing)position=0;notify();
}
void Recorder::seek(double seconds){if(recording||finalizing)return;const bool resume=playing&&!playPaused;stopPlayback();position=std::clamp(seconds,0.,duration);if(resume)play();notify();}
void Recorder::eject(){if(recording||finalizing)return;stopPlayback();file.clear();position=duration=0;notify();}
void Recorder::shutdown(){
    if(closing)return;
    closing=true;linkQueue.clear();clockTimer.stop();captureTimer.stop();stop();
    QElapsedTimer drain;drain.start();while(!pendingAudio.isEmpty()&&drain.elapsed()<10000){flushRecording();encoder.waitForBytesWritten(100);}
    flushRecording();
    if(encoder.state()!=QProcess::NotRunning&&!encoder.waitForFinished(10000)){encoder.kill();encoder.waitForFinished(1000);}
    capture.stop();
    for(auto* p:{&probe,&linker})if(p->state()!=QProcess::NotRunning){p->terminate();p->waitForFinished(1000);}
}
