#include "meter_readers.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QTemporaryDir>
#include <QtCore/QFile>
#include <cassert>
#include <unistd.h>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    if(args.contains("--hang")){QThread::sleep(10);return 0;}
    if(args.contains("--produce")){
        const float value=args[2].toFloat();QByteArray block(32768,Qt::Uninitialized);
        for(qsizetype i=0;i<block.size();i+=4)std::memcpy(block.data()+i,&value,4);
        for(int blockIndex=0;blockIndex<16;++blockIndex){qsizetype offset=0;while(offset<block.size()){const auto n=::write(STDOUT_FILENO,block.constData()+offset,block.size()-offset);if(n<=0)return 2;offset+=n;}}
        QFile marker(args[3]);if(!marker.open(QIODevice::WriteOnly))return 3;marker.write("done");return 0;
    }
    QTemporaryDir dir;assert(dir.isValid());MeterReaders readers;
    for(int i=0;i<MeterReaders::count;++i)readers.startMeter(i,app.applicationFilePath(),{"--produce",QString::number((i+1)*.05),dir.path()+QString("/%1").arg(i)});
    QElapsedTimer deadline;deadline.start();bool finished=false;
    // No GUI/main-thread event processing while all eleven pipes produce.
    while(!finished&&deadline.elapsed()<5000){finished=true;for(int i=0;i<MeterReaders::count;++i)finished=finished&&QFile::exists(dir.path()+QString("/%1").arg(i))&&readers.state(i)==QProcess::NotRunning;QThread::msleep(10);}
    assert(finished);
    for(int i=0;i<MeterReaders::count;++i){assert(readers.takeError(i).isEmpty());assert(std::abs(readers.takePeak(i)-(i+1)*.05)<1e-5);assert(readers.takePeak(i)==0);}
    readers.startMeter(0,"/nonexistent/voicemixer-test-meter",{});deadline.restart();
    while(readers.state(0)!=QProcess::NotRunning&&deadline.elapsed()<3000)QThread::msleep(10);
    assert(!readers.takeError(0).isEmpty());
    for(int i=0;i<MeterReaders::count;++i)readers.startMeter(i,app.applicationFilePath(),{"--hang"});
    QThread::msleep(50);deadline.restart();readers.stop();assert(deadline.elapsed()<1500);
    readers.startMeter(0,app.applicationFilePath(),{"--produce","0.75",dir.path()+"/restart"});deadline.restart();
    while(readers.state(0)!=QProcess::NotRunning&&deadline.elapsed()<3000)QThread::msleep(10);
    assert(QFile::exists(dir.path()+"/restart"));assert(readers.takePeak(0)==.75f);readers.stop();
}
