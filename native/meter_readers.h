#pragma once
#include <QtCore/QProcess>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>

// One I/O thread for the matrix meters. Retain peaks, never a growing audio
// backlog: the GUI can stop painting without blocking any pw-record pipe.
class MeterReaders:public QThread {
public:
    static constexpr int count=11;
private:
    struct Request{QString program;QStringList arguments;bool pending=false;};
    std::array<Request,count> requests;
    std::array<QString,count> errors;
    std::array<std::atomic<int>,count> states;
    std::array<std::atomic<float>,count> peaks;
    QMutex mutex;
protected:
    void run()override{
        QObject context;std::array<QProcess,count> processes;
        std::array<QByteArray,count> partial,diagnostics;
        for(int i=0;i<count;++i){
            auto& process=processes[i];
            QObject::connect(&process,&QProcess::stateChanged,&context,[this,i](QProcess::ProcessState state){states[i]=state;});
            QObject::connect(&process,&QProcess::readyReadStandardOutput,&context,[&,i]{
                auto& bytes=partial[i];bytes+=processes[i].readAllStandardOutput();
                const auto complete=bytes.size()/4*4;float peak=0;
                for(qsizetype offset=0;offset<complete;offset+=4){float value;std::memcpy(&value,bytes.constData()+offset,4);if(std::isfinite(value))peak=std::max(peak,std::abs(value));}
                bytes.remove(0,complete);
                float previous=peaks[i].load();while(previous<peak&&!peaks[i].compare_exchange_weak(previous,peak)){}
            });
            QObject::connect(&process,&QProcess::readyReadStandardError,&context,[&,i]{diagnostics[i]+=processes[i].readAllStandardError();diagnostics[i]=diagnostics[i].right(1024);});
            QObject::connect(&process,&QProcess::errorOccurred,&context,[&,i](QProcess::ProcessError error){if(error==QProcess::FailedToStart){QMutexLocker lock(&mutex);errors[i]=processes[i].errorString();}});
            QObject::connect(&process,&QProcess::finished,&context,[&,i](int code,QProcess::ExitStatus){if(code&&!isInterruptionRequested()){QMutexLocker lock(&mutex);errors[i]=QString("Meter %1 exited (%2): %3").arg(i).arg(code).arg(QString::fromUtf8(diagnostics[i]));}});
        }
        QTimer poll;QElapsedTimer stopping;poll.setInterval(20);
        QObject::connect(&poll,&QTimer::timeout,&context,[&]{
            if(isInterruptionRequested()){
                const bool first=!stopping.isValid();if(first)stopping.start();bool running=false;
                for(auto& process:processes)if(process.state()!=QProcess::NotRunning){running=true;if(first)process.terminate();else if(stopping.elapsed()>500)process.kill();}
                if(!running)quit();
                return;
            }
            std::array<Request,count> pending;
            {QMutexLocker lock(&mutex);for(int i=0;i<count;++i)if(requests[i].pending){pending[i]=std::move(requests[i]);requests[i]=Request{};}}
            for(int i=0;i<count;++i)if(pending[i].pending){partial[i].clear();diagnostics[i].clear();processes[i].start(pending[i].program,pending[i].arguments);}
        });poll.start();exec();
    }
public:
    MeterReaders(){for(int i=0;i<count;++i){states[i]=QProcess::NotRunning;peaks[i]=0;}}
    ~MeterReaders()override{stop();}
    QProcess::ProcessState state(int i)const{return QProcess::ProcessState(states[i].load());}
    void startMeter(int i,const QString& program,const QStringList& arguments){
        if(i<0||i>=count||state(i)!=QProcess::NotRunning)return;
        {QMutexLocker lock(&mutex);errors[i].clear();requests[i]={program,arguments,true};states[i]=QProcess::Starting;peaks[i]=0;}
        if(!isRunning())QThread::start();
    }
    float takePeak(int i){return peaks[i].exchange(0);}
    QString takeError(int i){QMutexLocker lock(&mutex);QString value;value.swap(errors[i]);return value;}
    void stop(){
        if(isRunning()){requestInterruption();wait();}
        QMutexLocker lock(&mutex);for(int i=0;i<count;++i){states[i]=QProcess::NotRunning;peaks[i]=0;errors[i].clear();requests[i]=Request{};}
    }
};
