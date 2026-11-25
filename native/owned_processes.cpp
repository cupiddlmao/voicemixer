#include "owned_processes.h"
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>
#include <QtCore/QRegularExpression>
#include <QtCore/QThread>
#include <QtCore/QDir>
#include <algorithm>
#include <csignal>
#include <unistd.h>

namespace {

QString location(const QString& runtime,const QString& kind){return runtime+"/"+kind+".pid";}

bool running(qint64 pid){return pid>1&&kill(static_cast<pid_t>(pid),0)==0;}

// One pid per line, newest last. A single-pid installation is already this format with one
// line, so an existing state directory keeps working untouched.
QList<qint64> read(const QString& runtime,const QString& kind) {
    QFile file(location(runtime,kind));QList<qint64> pids;
    if(!file.open(QIODevice::ReadOnly))return pids;
    for(const auto& token:QString::fromUtf8(file.readAll()).split(QRegularExpression("[^0-9]+"),Qt::SkipEmptyParts)) {
        const qint64 pid=token.toLongLong();
        if(pid>1&&!pids.contains(pid))pids.append(pid);
    }
    return pids;
}

void write(const QString& runtime,const QString& kind,const QList<qint64>& pids) {
    if(pids.isEmpty()){QFile::remove(location(runtime,kind));return;}
    QList<qint64> sorted=pids;std::sort(sorted.begin(),sorted.end());
    QByteArray body;for(const auto pid:sorted)body+=QByteArray::number(pid)+'\n';
    QSaveFile file(location(runtime,kind));
    if(!file.open(QIODevice::WriteOnly))return;
    file.write(body);file.commit();
}

// Poll with kill(pid,0) exactly like the graph teardown did before: the process has to be
// gone, not merely stopped, before we are willing to hand the node names to a new instance.
void waitForExit(const QString& runtime,const QString& kind,const QList<qint64>& pids,int ms) {
    if(pids.isEmpty())return;
    QElapsedTimer clock;clock.start();
    while(clock.elapsed()<ms) {
        bool any=false;for(const auto pid:pids)if(owned_processes::matches(runtime,kind,pid)){any=true;break;}
        if(!any)return;
        QThread::msleep(10);
    }
}

}

namespace owned_processes {

bool matches(const QString& runtime,const QString& kind,qint64 pid) {
    if(!running(pid))return false;
    QFile cmd(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if(!cmd.open(QIODevice::ReadOnly))return false;
    const auto args=cmd.readAll().split('\0');if(args.isEmpty())return false;
    const QString exe=QFileInfo(QString::fromUtf8(args.at(0))).fileName();
    if(kind=="engine")return exe=="pipewire"&&args.contains((runtime+"/microphone.conf").toUtf8());
    if(kind=="matrix")return exe=="pipewire"&&args.contains((runtime+"/matrix.conf").toUtf8());
    // The loopback kinds are retired as launchers but are still cleaned up on old state dirs.
    if(kind=="aux")return exe=="pw-loopback"&&args.contains(QByteArray("linux_mixer.aux.engine"));
    if(kind=="monitor")return exe=="pw-loopback"&&args.contains(QByteArray("linux_mixer.monitor"));
    return false;
}

QList<qint64> live(const QString& runtime,const QString& kind) {
    QList<qint64> kept;
    for(const auto pid:read(runtime,kind))if(matches(runtime,kind,pid))kept.append(pid);
    return kept;
}

qint64 newest(const QString& runtime,const QString& kind) {
    const auto pids=live(runtime,kind);return pids.isEmpty()?0:pids.last();
}

void record(const QString& runtime,const QString& kind,qint64 pid) {
    // A detached child's command line can lag its pid by a moment. Waiting briefly here is the
    // difference between tracking the graph and leaking it permanently.
    QElapsedTimer clock;clock.start();
    while(!matches(runtime,kind,pid)&&running(pid)&&clock.elapsed()<300)QThread::msleep(10);
    if(!matches(runtime,kind,pid))return;
    auto pids=live(runtime,kind);pids.removeAll(pid);pids.append(pid);
    write(runtime,kind,pids);
}

int adopt(const QString& runtime,const QString& kind) {
    // Only kinds identified by a conf path inside this state root can be adopted safely: the
    // retired loopback kinds are recognised by a stream name that another installation could use.
    if(kind!="engine"&&kind!="matrix")return 0;
    const auto recorded=read(runtime,kind);
    QList<qint64> pids;int added=0;
    for(const auto& entry:QDir("/proc").entryList(QDir::Dirs|QDir::NoDotAndDotDot)) {
        bool ok=false;const qint64 pid=entry.toLongLong(&ok);
        if(!ok||pid<=1||!matches(runtime,kind,pid))continue;
        if(!recorded.contains(pid))++added;
        pids.append(pid);
    }
    if(added)write(runtime,kind,pids);
    return added;
}

bool stopAll(const QString& runtime,const QString& kind,int termMs,int killMs) {
    const auto pids=live(runtime,kind);
    if(pids.isEmpty()){write(runtime,kind,QList<qint64>{});return true;}
    for(const auto pid:pids)kill(static_cast<pid_t>(pid),SIGTERM);
    waitForExit(runtime,kind,pids,termMs);
    QList<qint64> stubborn;for(const auto pid:pids)if(matches(runtime,kind,pid))stubborn.append(pid);
    for(const auto pid:stubborn)kill(static_cast<pid_t>(pid),SIGKILL);
    waitForExit(runtime,kind,stubborn,killMs);
    // Whatever refused to die stays recorded. Dropping it is how an orphan becomes permanent,
    // and a recorded process only ever blocks a relaunch until it is actually gone.
    const auto survivors=live(runtime,kind);
    write(runtime,kind,survivors);
    return survivors.isEmpty();
}

}
