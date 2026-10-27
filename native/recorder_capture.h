#pragma once
#include <QtCore/QProcess>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <deque>
#include <atomic>

// Drain pw-record independently of GUI painting, dialogs and graph discovery.
// The GUI takes complete frames from a bounded queue; it never owns this process.
class RecorderCapture:public QThread {
    QString executable;QStringList arguments;
    qsizetype limit=0,frameBytes=0,queued=0;
    std::deque<QByteArray> chunks;
    QMutex mutex;
    QString failure;
    bool overflow=false;
    std::atomic<QProcess::ProcessState> status{QProcess::NotRunning};
protected:
    void run()override{
        QProcess process;QByteArray partial;QElapsedTimer stopping;
        auto drain=[&]{
            partial+=process.readAllStandardOutput();
            const auto bytes=partial.size()/frameBytes*frameBytes;if(!bytes)return;
            auto audio=partial.left(bytes);partial.remove(0,bytes);
            QMutexLocker lock(&mutex);
            if(queued+audio.size()>limit){chunks.clear();queued=0;overflow=true;}
            if(audio.size()>limit)audio=audio.right(limit);
            queued+=audio.size();chunks.push_back(std::move(audio));
        };
        QObject::connect(&process,&QProcess::readyReadStandardOutput,&process,drain);
        QByteArray diagnostics;
        QObject::connect(&process,&QProcess::readyReadStandardError,&process,[&]{diagnostics+=process.readAllStandardError();diagnostics=diagnostics.right(8192);});
        QObject::connect(&process,&QProcess::started,&process,[&]{status=QProcess::Running;});
        QObject::connect(&process,&QProcess::errorOccurred,&process,[&](QProcess::ProcessError error){
            if(error==QProcess::FailedToStart){QMutexLocker lock(&mutex);failure=process.errorString();quit();}
        });
        QObject::connect(&process,&QProcess::finished,&process,[&](int code,QProcess::ExitStatus){
            drain();
            if(code&&!isInterruptionRequested()){QMutexLocker lock(&mutex);failure=QString::fromUtf8(diagnostics).trimmed();if(failure.isEmpty())failure=QString("Capture exited with code %1").arg(code);}
            quit();
        });
        QTimer stopTimer;stopTimer.setInterval(10);
        QObject::connect(&stopTimer,&QTimer::timeout,&process,[&]{
            if(!isInterruptionRequested())return;
            if(process.state()==QProcess::NotRunning){quit();return;}
            if(!stopping.isValid()){stopping.start();process.terminate();}
            else if(stopping.elapsed()>500)process.kill();
        });stopTimer.start();
        process.start(executable,arguments);exec();status=QProcess::NotRunning;
    }
public:
    ~RecorderCapture()override{stop();}
    QProcess::ProcessState state()const{return status.load();}
    void startProcess(const QString& program,const QStringList& args,int channels){
        if(state()!=QProcess::NotRunning)return;
        wait();executable=program;arguments=args;frameBytes=channels*4;limit=48000*frameBytes*2;
        {QMutexLocker lock(&mutex);chunks.clear();queued=0;overflow=false;failure.clear();}
        status=QProcess::Starting;QThread::start();
    }
    void stop(){
        if(isRunning()){requestInterruption();wait();}status=QProcess::NotRunning;
        QMutexLocker lock(&mutex);chunks.clear();queued=0;overflow=false;failure.clear();
    }
    QByteArray takeAudio(){
        std::deque<QByteArray> ready;qsizetype bytes;
        {QMutexLocker lock(&mutex);ready.swap(chunks);bytes=queued;queued=0;}
        QByteArray result;result.reserve(bytes);for(const auto& chunk:ready)result+=chunk;return result;
    }
    bool takeOverflow(){QMutexLocker lock(&mutex);const bool value=overflow;overflow=false;return value;}
    QString takeError(){QMutexLocker lock(&mutex);QString value;value.swap(failure);return value;}
};
