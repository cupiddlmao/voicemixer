#include "owned_processes.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileDevice>
#include <QtCore/QList>
#include <QtCore/QProcess>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <cassert>
#include <csignal>
#include <unistd.h>

static bool alive(qint64 pid){return pid>1&&kill(static_cast<pid_t>(pid),0)==0;}

// A process that looks exactly like a launched graph: argv[0] is what the controller sees
// after startDetached resolves the tool through PATH, and argv carries this state root's own
// conf path. `stubborn` ignores SIGTERM, which is what the escalation path has to get past.
static QProcess* spawnGraph(const QString& runtime,const QString& kind,QObject* parent,bool stubborn=false){
    const QString conf=runtime+"/"+kind+".conf";
    if(!QFile::exists(conf)){QFile file(conf);assert(file.open(QIODevice::WriteOnly));assert(file.write("{}")==2);file.close();}
    QString command=QString("exec -a pipewire tail -f '%1'").arg(conf);
    if(stubborn){
        QFile stub(runtime+"/stub.sh");assert(stub.open(QIODevice::WriteOnly));
        stub.write("#!/bin/sh\ntrap '' TERM\nwhile :; do sleep 0.2; done\n");stub.close();
        stub.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
        command=QString("exec -a pipewire sh '%1' '%2'").arg(runtime+"/stub.sh",conf);
    }
    auto* process=new QProcess(parent);
    process->setProgram("bash");process->setArguments({"-c",command});
    process->start();assert(process->waitForStarted(5000));
    return process;
}

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    QTemporaryDir dir;assert(dir.isValid());
    const QString runtime=dir.path(),kind="matrix";
    const qint64 outsider=QCoreApplication::applicationPid();

    assert(owned_processes::newest(runtime,kind)==0);
    assert(owned_processes::live(runtime,kind).isEmpty());
    // Nothing is ever signalled for a kind this state root does not launch.
    assert(owned_processes::newest(runtime,"engine")==0);
    assert(owned_processes::stopAll(runtime,"engine"));

    QList<QProcess*> spawned;
    auto* a=spawnGraph(runtime,kind,&app);spawned.append(a);
    owned_processes::record(runtime,kind,a->processId());
    assert(owned_processes::live(runtime,kind)==QList<qint64>{a->processId()});
    assert(owned_processes::newest(runtime,kind)==a->processId());

    // The regression: launching a replacement graph used to overwrite the file, leaving the
    // predecessor unaddressable while it still held the stable linux_mixer.* node names.
    auto* b=spawnGraph(runtime,kind,&app);spawned.append(b);
    owned_processes::record(runtime,kind,b->processId());
    // Hoisted out of assert(): a braced list carries a top-level comma, which older assert
    // macros read as two arguments (found by CI on GCC 13; GCC 16 accepts it).
    const QList<qint64> both{a->processId(),b->processId()};
    assert(owned_processes::live(runtime,kind)==both);
    assert(owned_processes::newest(runtime,kind)==b->processId());
    {QFile raw(runtime+"/"+kind+".pid");assert(raw.open(QIODevice::ReadOnly));
     const auto body=QString::fromUtf8(raw.readAll());
     assert(body.contains(QString::number(a->processId()))&&body.contains(QString::number(b->processId())));}

    // A process that is not ours is never recorded, even when we are asked to record it.
    owned_processes::record(runtime,kind,outsider);
    assert(owned_processes::live(runtime,kind).size()==2);

    // An exit the controller did not cause is noticed on the next read instead of being kept.
    kill(static_cast<pid_t>(b->processId()),SIGKILL);
    b->waitForFinished(5000);   // reap it, otherwise a zombie still passes a bare kill(pid,0)
    assert(!alive(b->processId()));
    assert(!owned_processes::live(runtime,kind).contains(b->processId()));
    assert(owned_processes::newest(runtime,kind)==a->processId());

    // A hand-edited or recycled file must not turn into a signal sent to strangers.
    {QFile raw(runtime+"/"+kind+".pid");assert(raw.open(QIODevice::WriteOnly));
     raw.write(QString("%1\n%2\n999999\n").arg(outsider).arg(a->processId()).toUtf8());}
    assert(owned_processes::live(runtime,kind)==QList<qint64>{a->processId()});

    // stopAll() reaches every recorded graph and escalates past a process that ignores TERM.
    auto* c=spawnGraph(runtime,kind,&app,true);spawned.append(c);
    owned_processes::record(runtime,kind,c->processId());
    assert(owned_processes::live(runtime,kind).size()==2);
    assert(owned_processes::stopAll(runtime,kind));
    assert(owned_processes::live(runtime,kind).isEmpty());
    assert(owned_processes::newest(runtime,kind)==0);
    assert(!QFile::exists(runtime+"/"+kind+".pid"));
    for(auto* process:spawned)process->waitForFinished(3000);   // reap before checking liveness
    assert(!alive(a->processId())&&!alive(c->processId()));
    assert(alive(outsider));   // we never touched the process that only looked similar

    // A graph whose pid was lost becomes reachable again through adoption instead of staying an
    // unaddressable orphan holding this state root's configuration.
    auto* loose=spawnGraph(runtime,kind,&app);spawned.append(loose);
    assert(owned_processes::live(runtime,kind).isEmpty());
    assert(owned_processes::adopt(runtime,kind)==1);
    assert(owned_processes::live(runtime,kind)==QList<qint64>{loose->processId()});
    assert(owned_processes::newest(runtime,kind)==loose->processId());
    assert(owned_processes::adopt(runtime,kind)==0);        // already recorded
    assert(owned_processes::adopt(runtime,"engine")==0);    // nothing of that kind here
    assert(owned_processes::stopAll(runtime,kind));
    assert(owned_processes::live(runtime,kind).isEmpty());
    for(auto* process:spawned)process->waitForFinished(3000);
    assert(!alive(loose->processId()));
    return 0;
}
