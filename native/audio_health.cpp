#include "audio_health.h"
#include <QtCore/QProcessEnvironment>
#include <QtCore/QRegularExpression>
#include <algorithm>
#include <cmath>
QList<AudioNodeHealth> parseAudioHealth(const QByteArray& bytes){
    QList<AudioNodeHealth> result;
    // FORMAT has a variable number of fields. NAME begins in its own column.
    const auto lines=QString::fromUtf8(bytes).split('\n');int nameColumn=-1;
    for(const auto& line:lines){
        if(line.startsWith("S ")&&line.contains("QUANT")&&line.contains("NAME")){result.clear();nameColumn=line.indexOf("NAME");continue;}
        if(nameColumn<0||line.size()<=nameColumn)continue;
        const auto fields=line.left(nameColumn).simplified().split(' ');if(fields.size()<9)continue;
        AudioNodeHealth node;node.state=fields[0];if(node.state=="C")continue;
        bool idOk=false,errorOk=false;node.id=fields[1].toInt(&idOk);node.quantum=fields[2].toInt();node.rate=fields[3].toInt();node.errors=fields[8].toULongLong(&errorOk);
        if(!idOk||!errorOk)continue;
        auto name=line.mid(nameColumn).trimmed();node.driver=!name.startsWith('+')&&!name.startsWith('=');
        if(!node.driver)name=name.mid(1).trimmed();
        node.name=name;
        bool loadOk=false;node.load=fields[node.driver?6:7].toDouble(&loadOk);if(!loadOk||!std::isfinite(node.load)||node.load<0)node.load=-1;
        result.append(node);
    }
    return result;
}
quint64 audioErrorDelta(const AudioNodeHealth& node,QHash<QString,quint64>& previous){
    // Suspended/creating rows have no profiler measurement. Their zero is
    // not a counter reset; using it as a baseline recounts historical xruns.
    if(node.state!="R"&&node.state!="T"&&node.state!="t")return 0;
    const auto key=QString::number(node.id)+":"+node.name;
    const bool known=previous.contains(key);const auto before=previous.value(key);
    previous.insert(key,node.errors);
    return known&&node.errors>=before?node.errors-before:0;
}
QList<QByteArray> AudioHealthStream::append(const QByteArray& data){
    QList<QByteArray> complete;partial+=data;
    if(partial.size()>256*1024){clear();return complete;}
    for(qsizetype end;(end=partial.indexOf('\n'))>=0;){
        const auto line=partial.left(end+1);partial.remove(0,end+1);
        if(line.startsWith("S ")&&line.contains("QUANT")&&line.contains("NAME")){
            if(!snapshot.isEmpty())complete.append(snapshot);
            snapshot=line;
        }else if(!snapshot.isEmpty())snapshot+=line;
        if(snapshot.size()>256*1024){clear();break;}
    }
    return complete;
}
AudioHealth::AudioHealth(QObject* parent,QString tool):QObject(parent),executable(std::move(tool)){
    poll.setInterval(5000);
    auto env=QProcessEnvironment::systemEnvironment();env.insert("LC_ALL","C");query.setProcessEnvironment(env);
    connect(&poll,&QTimer::timeout,this,[this]{sample();});
    connect(&query,&QProcess::readyReadStandardOutput,this,[this]{
        for(const auto& snapshot:stream.append(query.readAllStandardOutput()))consume(snapshot);
    });
    connect(&query,&QProcess::readyReadStandardError,this,[this]{query.readAllStandardError();});
    connect(&query,&QProcess::finished,this,[this](int,QProcess::ExitStatus){
        stream.clear();previous.clear();available=false;
        if(running){status="Audio statistics disconnected; retrying";if(changed)changed();}
    });
    connect(&query,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart){available=false;status="Install PipeWire tools to see audio statistics";if(changed)changed();}});
}
void AudioHealth::consume(const QByteArray& output){
    const auto parsed=parseAudioHealth(output);if(parsed.isEmpty())return;
    lastSnapshot.restart();available=true;nodes.clear();load=-1;periodMs=0;quantum=rate=0;
    // A new profiler client initially has placeholder counters, even on some
    // RUNNING rows. Establish its baseline before reporting any deltas.
    const bool warming=monitorAge.isValid()&&monitorAge.elapsed()<2000;
    status="Live audio statistics";
    for(auto node:parsed){
        // Driver W/Q is the graph processing load, not total machine CPU.
        if(node.driver&&(node.state=="R"||node.state=="T"||node.state=="t")&&node.quantum>0&&node.rate>0&&node.load>=load){load=node.load;quantum=node.quantum;rate=node.rate;periodMs=1000.*quantum/rate;}
        if(!node.name.startsWith("linux_mixer.")&&!relevant.contains(node.name))continue;
        const auto key=QString::number(node.id)+":"+node.name;
        const auto delta=audioErrorDelta(node,previous);if(!warming){totals[key]+=delta;errors+=delta;}
        node.sessionErrors=totals.value(key);nodes.append(node);
    }
    if(changed)changed();
}
AudioHealth::~AudioHealth(){stop();}
void AudioHealth::sample(){
    if(query.state()==QProcess::NotRunning){stream.clear();previous.clear();monitorAge.restart();lastSnapshot.restart();query.start(executable,{"-b"});}
    else if(lastSnapshot.isValid()&&lastSnapshot.elapsed()>15000){available=false;status="Audio statistics timed out; retrying";query.kill();if(changed)changed();}
}
void AudioHealth::start(){if(running)return;running=true;poll.start();sample();}
void AudioHealth::stop(){running=false;poll.stop();if(query.state()!=QProcess::NotRunning){query.kill();query.waitForFinished(500);}}
void AudioHealth::reset(){errors=0;totals.clear();for(auto& node:nodes)node.sessionErrors=0;if(changed)changed();}
