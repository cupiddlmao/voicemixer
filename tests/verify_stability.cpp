#include "audio_health.h"
#include "process_guard.h"
#include "update_checker.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QTemporaryDir>
#include <QtCore/QEventLoop>
#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QTextStream>
#include <QtCore/QFile>
#include <QtNetwork/QNetworkRequest>
#include <cassert>
#include <cstring>
class QDialog;
void styleMixerDialog(QDialog&){} // The headless tests never open a widget.
namespace {
QJsonObject release(QString tag,bool preview=false,bool draft=false,QString url={}){
    return {{"tag_name",tag},{"prerelease",preview},{"draft",draft},{"html_url",url.isEmpty()?"https://github.com/cupiddlmao/voicemixer/releases/tag/"+tag:url}};
}
void waitUntil(const std::function<bool()>& ready,int timeout=2500){
    QElapsedTimer timer;timer.start();while(!ready()&&timer.elapsed()<timeout){QCoreApplication::processEvents(QEventLoop::AllEvents,10);}assert(ready());
}
class Reply : public QNetworkReply {
    QByteArray body;qsizetype offset=0;
public:
    Reply(const QNetworkRequest& request,QByteArray data,int code,QObject* parent):QNetworkReply(parent),body(std::move(data)){
        setRequest(request);setUrl(request.url());setOperation(QNetworkAccessManager::GetOperation);setAttribute(QNetworkRequest::HttpStatusCodeAttribute,code);open(QIODevice::ReadOnly|QIODevice::Unbuffered);
        QTimer::singleShot(1,this,[this]{emit readyRead();if(!isFinished()){setFinished(true);emit finished();}});
    }
    void abort()override{if(!isFinished()){setError(OperationCanceledError,"aborted");setFinished(true);emit finished();}}
    qint64 bytesAvailable()const override{return body.size()-offset+QNetworkReply::bytesAvailable();}
    qint64 readData(char* data,qint64 count)override{const auto n=qMin<qint64>(count,body.size()-offset);if(n<=0)return -1;std::memcpy(data,body.constData()+offset,n);offset+=n;return n;}
};
class Transport : public QNetworkAccessManager {
public:
    QByteArray body;int code=200,requests=0;
    QNetworkReply* createRequest(Operation op,const QNetworkRequest& request,QIODevice*)override{
        assert(op==GetOperation);assert(request.url().host()=="api.github.com");assert(!request.hasRawHeader("Authorization"));++requests;return new Reply(request,body,code,this);
    }
};
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QTemporaryDir profile;assert(profile.isValid());qputenv("LINUX_MIXER_STATE_DIR",profile.path().toUtf8());
    QString error;
    const auto releases=QJsonDocument(QJsonArray{release("v0.3.2"),release("v0.6.0",true),release("v0.4.1"),release("v9.0.0",false,true)}).toJson();
    assert(newestUpdate(releases,"0.4.0",false,&error).version=="0.4.1");assert(error.isEmpty());
    assert(newestUpdate(releases,"0.4.0",true).version=="0.6.0");
    assert(newestUpdate(releases,"0.6.0",true).version.isEmpty());
    assert(newestUpdate("not json","0.4.0",true,&error).version.isEmpty()&&!error.isEmpty());
    assert(newestUpdate("{}","0.4.0",true,&error).version.isEmpty()&&!error.isEmpty());
    for(const auto& bad:{release("v99.0.0",false,false,"https://evil.example/file"),release("v99.0.0",false,false,"https://github.com/other/repo/releases/tag/v99.0.0"),release("v99.0.0",false,false,"http://github.com/cupiddlmao/voicemixer/releases/tag/v99.0.0"),release("v1.0.0.exe"),release("v1.0.0-rc1")})assert(newestUpdate(QJsonDocument(QJsonArray{bad}).toJson(),"0.4.0",true).url.isEmpty());
    assert(newestUpdate(QJsonDocument(QJsonArray{release("v0.10.0"),release("v0.9.0")}).toJson(),"0.4.0",false).version=="0.10.0");
    Transport transport;
    {
        UpdateChecker checker(nullptr,&transport);assert(transport.requests==0); // No default network request.
        transport.body=releases;checker.check();checker.check();assert(transport.requests==1);waitUntil([&]{return !checker.busy;});assert(checker.update.version=="0.6.0");
        transport.body="{}";checker.check();waitUntil([&]{return !checker.busy;});assert(checker.update.url.isEmpty()&&checker.status.contains("invalid"));
        transport.code=403;checker.check();waitUntil([&]{return !checker.busy;});assert(checker.status.contains("request limit"));
        transport.code=200;transport.body=QByteArray(300*1024,'x');checker.check();waitUntil([&]{return !checker.busy;});assert(checker.status.contains("size limit"));
        transport.body=releases;checker.check();waitUntil([&]{return !checker.busy;});assert(checker.update.version=="0.6.0"); // Recover after failure.
    }
    const QByteArray header="S   ID  QUANT   RATE    WAIT    BUSY   W/Q   B/Q  ERR FORMAT           NAME \n";
    const QByteArray lines="R  714    512  48000   1.3ms   2.2us  0.12  0.00   83    S16LE 1 48000 alsa_input.usb-Mic\nR  547      0  48000  14.3us  88.4us  0.00  0.01    0                   + linux_mixer.plugins.input0\n";
    auto nodes=parseAudioHealth(header+lines);assert(nodes.size()==2);assert(nodes[0].quantum==512&&nodes[0].rate==48000&&nodes[0].errors==83&&nodes[0].load==.12&&nodes[0].driver);
    QHash<QString,quint64> history;auto counter=nodes[0];
    assert(audioErrorDelta(counter,history)==0); // First observation is the baseline.
    counter.state="S";counter.errors=0;assert(audioErrorDelta(counter,history)==0);
    counter.state="R";counter.errors=83;assert(audioErrorDelta(counter,history)==0); // No phantom errors after suspend.
    counter.errors=86;assert(audioErrorDelta(counter,history)==3);
    counter.errors=2;assert(audioErrorDelta(counter,history)==0); // Real counter restart.
    counter.errors=4;assert(audioErrorDelta(counter,history)==2);
    counter.state="I";counter.errors=0;assert(audioErrorDelta(counter,history)==0);
    counter.state="R";counter.errors=4;assert(audioErrorDelta(counter,history)==0);
    const auto frame=[&](int count){auto row=lines;row.replace("   83 ",QByteArray::number(count).rightJustified(5,' ')+" ");return header+row;};
    AudioHealthStream stream;QList<QByteArray> complete;
    const auto wire=frame(91)+frame(91)+frame(92)+header;
    for(int offset=0;offset<wire.size();offset+=7)complete.append(stream.append(wire.mid(offset,7)));
    assert(complete.size()==3);QHash<QString,quint64> continuous;quint64 continuousErrors=0;
    for(const auto& snapshot:complete)for(const auto& node:parseAudioHealth(snapshot))continuousErrors+=audioErrorDelta(node,continuous);
    assert(continuousErrors==1); // Historical 91 stays flat; a real increment is counted.
    stream.clear();assert(stream.append(QByteArray(300*1024,'x')).isEmpty());
    // Exercise the asynchronous persistent reader, including RUNNING placeholders.
    {
        QTemporaryDir tools;assert(tools.isValid());QFile helper(tools.path()+"/pw-top");assert(helper.open(QIODevice::WriteOnly));
        const auto quoted=[](QByteArray value){return "'"+value.replace("'","'\\''")+"'";};
        const QByteArray script="#!/bin/sh\nprintf '%s' "+quoted(frame(0)+frame(91)+frame(91))+"\nsleep 2.2\nprintf '%s' "+quoted(frame(92)+frame(92)+header)+"\nsleep 10\n";
        helper.write(script);helper.close();assert(helper.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        AudioHealth health(nullptr,helper.fileName());health.setRelevantNodes({"alsa_input.usb-Mic"});health.start();
        waitUntil([&]{return health.available&&health.errors==1;},4500);assert(health.nodes[0].sessionErrors==1);
        health.reset();assert(health.errors==0);health.stop();
    }
    assert(nodes[1].name=="linux_mixer.plugins.input0"&&!nodes[1].driver&&nodes[1].load==.01);
    assert(parseAudioHealth(header+lines+header+lines).size()==2); // Keep the last snapshot only.
    assert(parseAudioHealth("garbage").isEmpty());assert(parseAudioHealth(header+"C  1 0 0 --- --- --- --- 0 x\n").isEmpty());
    QProcess stalled;guardCommand(stalled,50);stalled.start("/usr/bin/sleep",{"5"});waitUntil([&]{return stalled.state()==QProcess::NotRunning;});assert(stalled.property("timedOut").toBool());
    stalled.start("/usr/bin/true");waitUntil([&]{return stalled.state()==QProcess::NotRunning;});assert(!stalled.property("timedOut").toBool()&&stalled.exitCode()==0);
    if(argc>1&&QString::fromLocal8Bit(argv[1])=="--monitor"){
        AudioHealth health;QStringList relevant;for(int i=2;i<argc;++i)relevant<<QString::fromLocal8Bit(argv[i]);health.setRelevantNodes(relevant);int samples=0;
        health.changed=[&]{if(!health.available)return;++samples;QTextStream(stdout)<<"monitor sample="<<samples<<" errors="<<health.errors<<" nodes="<<health.nodes.size()<<"\n";};
        health.start();QTimer::singleShot(18000,&app,&QCoreApplication::quit);app.exec();health.stop();assert(samples>=5);return 0;
    }
    if(argc>1){QFile file(QString::fromLocal8Bit(argv[1]));assert(file.open(QIODevice::ReadOnly));const auto real=parseAudioHealth(file.readAll());assert(!real.empty());bool mixer=false;for(const auto& n:real)if(n.name.startsWith("linux_mixer."))mixer=true;assert(mixer);}
    QTextStream(stdout)<<"PASS: release versions/channels/URLs, asynchronous update failures and size limits, no default network requests, PipeWire statistics parsing, hung-command timeout and reuse.\n";
}
