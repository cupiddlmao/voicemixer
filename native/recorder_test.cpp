// Exercise the actual recorder against isolated virtual sources and buses.
#include "recorder.h"
#include "routing_settings.h"
#include <QtWidgets/QApplication>
#include <QtCore/QTemporaryDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QTextStream>
#include <QtCore/QElapsedTimer>
#include <QtCore/QDir>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace {
class Test:public QObject {
    QApplication& app;QTemporaryDir dir;RecorderSettings settings;QString prefix;RoutingSettings routing;
    std::unique_ptr<Recorder> recorder;QProcess matrix,dump;std::array<QProcess,3> players;QProcess reader,silent;
    QTimer registry,feed,tick;QElapsedTimer timeout,phaseTime;int phase=0,formatIndex=0;QString firstFile;QByteArray heard,unrouted;double pauseAt=0;bool linked=false;
    QJsonArray latest;
    void require(bool v,const char* msg){if(!v)throw std::runtime_error(msg);}
    void advance(int next){phase=next;phaseTime.restart();}
    QJsonObject probe(const QString& file){QProcess p;p.start(mediaTool("ffprobe"),{"-v","error","-show_streams","-show_format","-of","json",file});require(p.waitForFinished(3000)&&p.exitCode()==0,"ffprobe failed");return QJsonDocument::fromJson(p.readAllStandardOutput()).object();}
    QByteArray decode(const QString& file){QProcess p;p.start(mediaTool("ffmpeg"),{"-v","error","-i",file,"-f","f32le","-acodec","pcm_f32le","pipe:1"});require(p.waitForFinished(3000)&&p.exitCode()==0,"decode failed");return p.readAllStandardOutput();}
    double amplitude(const QByteArray& bytes,int channels,int channel,int frequency,int rate=48000){const int n=std::min<int>(bytes.size()/(channels*4),9600);require(n>4000,"insufficient recorded audio");double re=0,im=0;for(int i=0;i<n;++i){float value;std::memcpy(&value,bytes.constData()+(i*channels+channel)*4,4);require(std::isfinite(value),"invalid recorded samples");double window=.5-.5*std::cos(2*M_PI*i/(n-1)),angle=2*M_PI*frequency*i/rate;re+=value*window*std::cos(angle);im+=value*window*std::sin(angle);}return std::hypot(re,im)*4/n;}
    bool node(const QString& name){for(const auto& n:latest)if(n.toObject().value("info").toObject().value("props").toObject().value("node.name")==name)return true;return false;}
    int port(const QString& name,const QString& channel,const QString& direction){int id=-1;for(const auto& v:latest){auto n=v.toObject();if(n.value("type")=="PipeWire:Interface:Node"&&n.value("info").toObject().value("props").toObject().value("node.name")==name)id=n.value("id").toInt();}for(const auto& v:latest){auto n=v.toObject(),info=n.value("info").toObject(),p=info.value("props").toObject();if(n.value("type")=="PipeWire:Interface:Port"&&p.value("node.id").toInt(-2)==id&&p.value("audio.channel")==channel&&info.value("direction")==direction)return n.value("id").toInt();}return -1;}
    void link(int out,int in){require(out>=0&&in>=0,"missing test port");QProcess p;p.start("pw-link",{QString::number(out),QString::number(in)});require(p.waitForFinished(1500),"link timed out");}
    void finish(int status,const QString& message){tick.stop();feed.stop();registry.stop();if(recorder)recorder->shutdown();for(auto* p:{&reader,&silent,&dump,&players[0],&players[1],&players[2],&matrix})if(p->state()!=QProcess::NotRunning){p->terminate();p->waitForFinished(1000);}QTextStream(status?stderr:stdout)<<message<<'\n';app.exit(status);}
    void step(){
        try{
            if(timeout.elapsed()>35000)throw std::runtime_error("recorder test timeout");
            if(!recorder->error.isEmpty())throw std::runtime_error(recorder->error.toStdString());
            if(phase==0){if(!node(prefix+".recorder.capture")||!node(prefix+".b3"))return;
                for(int i=0;i<3;++i){players[i].start("pw-play",{"--raw","--target",virtualInputName(i,prefix),"--rate","48000","--channels","2","--format","f32","--latency","256","--properties","node.dont-fallback=true state.restore-props=false state.restore-target=false state.default-volume=1.0","-"});
                    for(int side=0;side<2;++side)link(port(virtualInputName(i,prefix),side?"FR":"FL","output"),port(busCaptureName(5,prefix),QString("AUX%1").arg(10+i*2+side),"input"));}
                advance(1);
            }else if(phase==1&&phaseTime.elapsed()>1100&&recorder->inputReady){recorder->record();advance(2);}
            else if(phase==2&&recorder->position>.55){recorder->record();pauseAt=recorder->position;advance(3);}
            else if(phase==3&&phaseTime.elapsed()>250){require(recorder->recordPaused&&std::abs(recorder->position-pauseAt)<.001,"REC pause changed recorded duration");recorder->record();advance(4);}
            else if(phase==4&&!recorder->recording&&!recorder->finalizing){
                firstFile=recorder->recordFile;auto metadata=probe(firstFile),stream=metadata.value("streams").toArray()[0].toObject();require(stream.value("channels").toInt()==2,"pre-fader recording was not stereo");require(std::abs(metadata.value("format").toObject().value("duration").toString().toDouble()-1.2)<.01,"record timer/pre-roll duration wrong");
                const auto audio=decode(firstFile);QTextStream(stdout)<<"Pre-fader amplitude "<<amplitude(audio,2,0,333)<<" / "<<amplitude(audio,2,0,777)<<" right "<<amplitude(audio,2,1,777)<<"\n";require(std::abs(amplitude(audio,2,0,333)-.04)<.003&&amplitude(audio,2,0,777)<.002,"raw input was affected by strip mute/EQ or lost stereo");require(QDir(settings.directory).entryList({"*_Track1.wav"}).size()==1&&QDir(settings.directory).entryList({"*_Track2.wav"}).size()==1,"multitrack files missing");
                settings.busMode=true;settings.bus=5;settings.channels=8;settings.format="BWF";settings.bits=24;settings.rate=44100;settings.preSeconds=0;settings.stopAfter=.35;settings.prefix="Composite";recorder->configure();advance(5);
            }else if(phase==5&&phaseTime.elapsed()>500&&recorder->inputReady){recorder->record();advance(6);}
            else if(phase==6&&!recorder->recording&&!recorder->finalizing){
                auto meta=probe(recorder->recordFile);auto stream=meta.value("streams").toArray()[0].toObject();require(stream.value("channels").toInt()==8&&stream.value("sample_rate").toString()=="44100"&&stream.value("codec_name")=="pcm_s24le","BWF channel count/rate/bit depth mismatch");QFile f(recorder->recordFile);require(f.open(QIODevice::ReadOnly)&&f.read(4096).contains("bext"),"BWF broadcast chunk missing");require(QDir(settings.directory).entryList({"Composite*_Track*.wav"}).size()==8,"8-channel track exports missing");const auto eight=decode(recorder->recordFile);const int tones[8]={333,777,555,666,888,999,333,777};for(int c=0;c<8;++c){QTextStream(stdout)<<"Composite "<<c<<": "<<amplitude(eight,8,c,tones[c],44100)<<"\n";require(std::abs(amplitude(eight,8,c,tones[c],44100)-.04*std::pow(10.,-6./20))<.003,"Composite track content/routing incorrect");}
                settings.busMode=false;settings.channels=2;settings.preSeconds=0;settings.rate=48000;settings.stopAfter=.3;settings.multitrack=false;settings.format="AIFF";settings.prefix="Aiff";settings.bits=24;recorder->configure();advance(7);
            }else if(phase==7&&phaseTime.elapsed()>250&&recorder->inputReady){recorder->record();advance(8);}
            else if(phase==8&&!recorder->recording&&!recorder->finalizing){require(probe(recorder->recordFile).value("streams").toArray()[0].toObject().value("codec_name")=="pcm_s24be","AIFF encoding failed");settings.format="MP3";settings.prefix="Mp3";settings.stopAfter=.4;recorder->configure();advance(9);}
            else if(phase==9&&phaseTime.elapsed()>250&&recorder->inputReady){recorder->record();advance(10);}
            else if(phase==10&&!recorder->recording&&!recorder->finalizing){require(probe(recorder->recordFile).value("streams").toArray()[0].toObject().value("codec_name")=="mp3","MP3 encoding failed");settings.format="WAV";settings.prefix="Float";settings.bits=32;recorder->configure();advance(11);}
            else if(phase==11&&phaseTime.elapsed()>250&&recorder->inputReady){recorder->record();advance(12);}
            else if(phase==12&&!recorder->recording&&!recorder->finalizing){require(probe(recorder->recordFile).value("streams").toArray()[0].toObject().value("codec_name")=="pcm_f32le","float WAV encoding failed");settings.playOnLoad=true;settings.loop=true;recorder->loadFile(firstFile);reader.start("pw-record",{"--raw","--target",busOutputName(6,prefix),"--properties","node.dont-fallback=true state.restore-props=false state.default-volume=1.0","--rate","48000","--channels","2","--format","f32","--latency","256","-"});silent.start("pw-record",{"--raw","--target",busOutputName(7,prefix),"--properties","node.dont-fallback=true state.restore-props=false state.default-volume=1.0","--rate","48000","--channels","2","--format","f32","--latency","256","-"});advance(13);}
            else if(phase==13&&recorder->playing&&recorder->position>.35){recorder->pause();pauseAt=recorder->position;advance(14);}
            else if(phase==14&&phaseTime.elapsed()>200){require(recorder->playPaused&&std::abs(recorder->position-pauseAt)<.01,"play pause did not hold cursor");recorder->pause();recorder->seek(.8);heard.clear();unrouted.clear();advance(15);}
            else if(phase==15&&phaseTime.elapsed()>1500){QTextStream(stdout)<<"Loop state: "<<recorder->playing<<" paused "<<recorder->playPaused<<" completed "<<recorder->completedPlays<<" pos "<<recorder->position<<"\n";require(recorder->playing&&recorder->completedPlays>=1,"loop playback stopped");require(heard.size()>16000,"playback did not reach selected bus");auto tail=heard.right(std::min<qsizetype>(heard.size(),96000));require(amplitude(tail,2,0,333)>.012,"playback route/gain lost audio");for(qsizetype i=0;i+4<=unrouted.size();i+=4){float v;std::memcpy(&v,unrouted.constData()+i,4);require(std::abs(v)<1e-6,"playback leaked to an unselected bus");}recorder->stop();recorder->eject();require(recorder->file.isEmpty(),"eject failed");finish(0,"PASS: pre-fader stereo despite strip mute, pre-roll, REC pause/resume, duration timer, WAV/BWF/AIFF/MP3, float/24-bit encoding, independent sample rate, 8-channel Composite/mono tracks, play-on-load, pause, seek, loop and routed playback. No physical audio devices used.");}
        }catch(const std::exception& ex){finish(1,QString("FAIL: ")+ex.what());}
    }
public:
    Test(QApplication& app):QObject(&app),app(app),prefix("recorder_test_"+QString::number(QCoreApplication::applicationPid())){
        settings.directory=dir.path();settings.armed.fill(false);settings.armed[5]=true;settings.preSeconds=.25;settings.stopAfter=1.2;settings.multitrack=true;settings.playback.fill(false);settings.playback[6]=true;settings.gain=-6;
        routing.busGain[5]=-6;routing.inputMute[0]=true;routing.busComposite[5]=true;routing.compositePatch[5]={10,11,12,13,14,15,10,11};
        QFile config(dir.path()+"/matrix.conf");require(config.open(QIODevice::WriteOnly),"cannot create test graph");config.write(QJsonDocument(matrixConfiguration(routing,prefix,&settings)).toJson());config.close();matrix.start("pipewire",{"-c",config.fileName()});
        recorder=std::make_unique<Recorder>(settings,[]{return std::array<RecorderSource,5>{};},this,prefix);
        connect(&dump,&QProcess::finished,this,[this](int code,QProcess::ExitStatus){if(!code){latest=QJsonDocument::fromJson(dump.readAllStandardOutput()).array();recorder->consume(latest);}});
        auto refresh=[this]{if(dump.state()==QProcess::NotRunning)dump.start("pw-dump");};recorder->needGraph=refresh;registry.setInterval(180);connect(&registry,&QTimer::timeout,this,refresh);registry.start();
        connect(&reader,&QProcess::readyReadStandardOutput,this,[this]{heard+=reader.readAllStandardOutput();});connect(&silent,&QProcess::readyReadStandardOutput,this,[this]{unrouted+=silent.readAllStandardOutput();});
        feed.setInterval(30);connect(&feed,&QTimer::timeout,this,[this]{static std::array<int,3> frame{};for(int input=0;input<3;++input)if(players[input].state()==QProcess::Running&&players[input].bytesToWrite()<48000){QByteArray bytes(2400*8,Qt::Uninitialized);for(int f=0;f<2400;++f)for(int c=0;c<2;++c){const int frequencies[6]={333,777,555,666,888,999};float v=.04*std::sin(2*M_PI*frequencies[input*2+c]*(frame[input]+f)/48000.);std::memcpy(bytes.data()+(f*2+c)*4,&v,4);}players[input].write(bytes);frame[input]+=2400;}});feed.start();
        tick.setInterval(50);connect(&tick,&QTimer::timeout,this,[this]{step();});timeout.start();phaseTime.start();tick.start();
    }
};
}
void runRecorderTest(QApplication& app){new Test(app);}
