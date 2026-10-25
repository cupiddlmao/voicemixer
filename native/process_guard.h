#pragma once
#include <QtCore/QProcess>
#include <QtCore/QTimer>
#include <QtCore/QVariant>

// A lost PipeWire/Pulse connection must not hold a command queue forever.
// Only attach to short-lived control/query commands, never audio streams.
inline void guardCommand(QProcess& process,int timeoutMs=5000) {
    auto* timer=new QTimer(&process);timer->setSingleShot(true);timer->setInterval(timeoutMs);
    QObject::connect(&process,&QProcess::started,timer,[&process,timer]{process.setProperty("timedOut",false);timer->start();});
    QObject::connect(&process,&QProcess::finished,timer,[timer]{timer->stop();});
    QObject::connect(&process,&QProcess::errorOccurred,timer,[timer](QProcess::ProcessError error){if(error==QProcess::FailedToStart)timer->stop();});
    QObject::connect(timer,&QTimer::timeout,&process,[&process]{if(process.state()!=QProcess::NotRunning){process.setProperty("timedOut",true);process.kill();}});
}
