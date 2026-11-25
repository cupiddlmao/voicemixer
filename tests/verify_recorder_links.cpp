#include "recorder.h"
#include "routing_settings.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileDevice>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <cassert>

// Graph helpers in the shape Recorder::consume() reads (PipeWire node/port JSON).
static QJsonObject node(int id,const QString& name){return QJsonObject{{"id",id},{"type","PipeWire:Interface:Node"},{"info",QJsonObject{{"props",QJsonObject{{"node.name",name}}}}}};}
static QJsonObject port(int id,int nodeId,const QString& channel,const QString& direction){
    return QJsonObject{{"id",id},{"type","PipeWire:Interface:Port"},{"info",QJsonObject{{"direction",direction},{"props",QJsonObject{{"node.id",nodeId},{"audio.channel",channel}}}}}};
}

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    QTemporaryDir dir;assert(dir.isValid());
    // A fake pw-link that records each link and stays busy for 200 ms, so the
    // linker is still running when the next graph refresh arrives.
    const QString log=dir.path()+"/links.log";
    QDir().mkpath(dir.path()+"/bin");
    QFile tool(dir.path()+"/bin/pw-link");assert(tool.open(QIODevice::WriteOnly));
    tool.write(QString("#!/bin/sh\necho \"$1 $2\" >> '%1'\nsleep 0.2\n").arg(log).toUtf8());tool.close();
    tool.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
    qputenv("PATH",(dir.path()+"/bin:"+qEnvironmentVariable("PATH")).toUtf8());

    static std::array<RecorderSource,5> sources;
    RecorderSettings settings;
    Recorder recorder(settings,[]{return sources;},&app,"linux_mixer");
    recorder.setActive(true);

    const QString prefix="linux_mixer";
    QJsonArray graph;
    graph.append(node(1,virtualInputName(0,prefix)));graph.append(port(10,1,"FL","output"));graph.append(port(11,1,"FR","output"));
    graph.append(node(2,virtualInputName(1,prefix)));graph.append(port(20,2,"FL","output"));graph.append(port(21,2,"FR","output"));
    graph.append(node(3,virtualInputName(2,prefix)));graph.append(port(30,3,"FL","output"));graph.append(port(31,3,"FR","output"));
    graph.append(node(4,prefix+".recorder.capture"));
    for(int k=0;k<16;++k)graph.append(port(100+k,4,QString("AUX%1").arg(k),"input"));
    // Refresh 1: the three virtual inputs are wired to the capture node (six links).
    recorder.consume(graph);
    // Refresh 2 arrives while the linker is still busy and adds a hardware input (two new links).
    graph.append(node(5,"alsa_input.test"));graph.append(port(50,5,"FL","output"));
    sources[0]={"alsa_input.test","FL"};
    recorder.consume(graph);

    // No further graph refreshes: every desired link must still be created.
    const QStringList expected{"10 110","11 111","20 112","21 113","30 114","31 115","50 100","50 101"};
    QElapsedTimer deadline;deadline.start();QStringList seen;
    while(deadline.elapsed()<10000){
        QCoreApplication::processEvents(QEventLoop::AllEvents,20);QThread::msleep(10);
        QFile in(log);seen.clear();if(in.open(QIODevice::ReadOnly))seen=QString::fromUtf8(in.readAll()).split('\n',Qt::SkipEmptyParts);
        if(seen.size()>=expected.size())break;
    }
    QThread::msleep(400);QCoreApplication::processEvents(QEventLoop::AllEvents,50);
    QFile in(log);assert(in.open(QIODevice::ReadOnly));
    seen=QString::fromUtf8(in.readAll()).split('\n',Qt::SkipEmptyParts);
    // Re-issuing a link that has not appeared in the graph yet is harmless; only the set matters.
    for(const auto& pair:expected)assert(seen.contains(pair));
    for(const auto& pair:seen)assert(expected.contains(pair));
    return 0;
}
