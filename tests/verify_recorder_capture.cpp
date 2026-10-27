#include "recorder_capture.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <cassert>
#include <unistd.h>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    if(args.contains("--hang")){QThread::sleep(10);return 0;}
    if(args.contains("--produce")){
        const int count=args[2].toInt();
        for(int i=0;i<count;++i){
            QByteArray bytes(32768,char(i));qsizetype offset=0;
            while(offset<bytes.size()){const auto n=::write(STDOUT_FILENO,bytes.constData()+offset,bytes.size()-offset);if(n<=0)return 2;offset+=n;}
        }
        QFile done(args[3]);if(!done.open(QIODevice::WriteOnly))return 3;done.write("done");return 0;
    }
    QTemporaryDir dir;assert(dir.isValid());RecorderCapture capture;
    auto run=[&](int count,const QString& marker){
        capture.startProcess(app.applicationFilePath(),{"--produce",QString::number(count),marker},16);
        // Deliberately don't run this thread's event loop or consume audio.
        // A QProcess on the UI thread would fill its OS pipe and stall here.
        QElapsedTimer timer;timer.start();
        while(!QFile::exists(marker)&&timer.elapsed()<3000)QThread::msleep(10);
        assert(QFile::exists(marker));
        while(capture.state()!=QProcess::NotRunning&&timer.elapsed()<3500)QThread::msleep(10);
        assert(capture.state()==QProcess::NotRunning);assert(capture.takeError().isEmpty());
    };
    run(128,dir.path()+"/first");
    assert(!capture.takeOverflow());const auto audio=capture.takeAudio();assert(audio.size()==128*32768);
    for(int i=0;i<128;++i)assert(audio.mid(i*32768,32768)==QByteArray(32768,char(i)));
    run(512,dir.path()+"/overflow");assert(capture.takeOverflow());
    const auto bounded=capture.takeAudio();assert(!bounded.isEmpty()&&bounded.size()<=48000*16*4*2&&bounded.size()%64==0);
    capture.startProcess("/nonexistent/voicemixer-test-command",{},16);
    QElapsedTimer timer;timer.start();while(capture.state()!=QProcess::NotRunning&&timer.elapsed()<3000)QThread::msleep(10);
    assert(!capture.takeError().isEmpty());capture.stop();
    capture.startProcess(app.applicationFilePath(),{"--hang"},16);
    QThread::msleep(30);timer.restart();capture.stop();
    assert(timer.elapsed()<1500&&capture.state()==QProcess::NotRunning);
    assert(capture.takeAudio().isEmpty()&&!capture.takeOverflow());
}
