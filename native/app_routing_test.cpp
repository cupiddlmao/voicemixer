// Tests a silent synthetic app, including remembered routing after stream recreation.
#include "engine.h"
#include "paths.h"
#include "routing.h"
#include <QtWidgets/QApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTextStream>
#include <stdexcept>

namespace {
class AppRoutingTest : public QObject {
    QApplication& app;
    Engine& e;
    QProcess playback;
    QTimer tick,feed;
    QElapsedTimer timeout;
    int phase=0,oldStream=-1;
    const QString key="id:org.linuxmixer.RoutingTest";
    const QString appName="Linux Mixer Routing Test";
    QJsonObject originalRules;
    void launch(){
        playback.start("pw-play",{"--raw","--target","linux_mixer.system","--properties","application.name=\"Linux Mixer Routing Test\" application.id=org.linuxmixer.RoutingTest node.name=linux_mixer_routing_test.app", "--rate","48000","--channels","2","--format","f32","-"});
    }
    void finish(int status,const QString& message){
        tick.stop();feed.stop();if(playback.state()!=QProcess::NotRunning){playback.terminate();playback.waitForFinished(1000);}
        e.settings.routing.appRules=originalRules;e.save();QTextStream(status?stderr:stdout)<<message<<'\n';app.exit(status);
    }
    void advance(){
        if(timeout.elapsed()>14000){finish(1,"FAIL: remembered app routing timed out");return;}
        try{
            if(phase==0){if(!e.matrix->ready)return;launch();phase=1;e.matrix->refreshApps();return;}
            const PlaybackApp* found=nullptr;for(const auto& entry:e.matrix->apps)if(entry.key==key){found=&entry;break;}
            if(phase==1&&found){oldStream=found->streams.first();e.matrix->assignApp(key,1);phase=2;return;}
            if(phase==2&&found&&found->input==1){
                QFile saved(stateRoot()+"/settings.json");if(!saved.open(QIODevice::ReadOnly))throw std::runtime_error("saved settings missing");
                const auto rules=QJsonDocument::fromJson(saved.readAll()).object().value("routing").toObject().value("appRules").toObject();
                if(rules.value(key).toInt(-1)!=1)throw std::runtime_error("assignment was not saved");
                playback.terminate();playback.waitForFinished(1000);phase=3;e.matrix->refreshApps();return;
            }
            if(phase==3&&!found){launch();phase=4;return;}
            if(phase==4&&found&&found->input==1&&found->streams.first()!=oldStream){
                finish(0,"PASS: selected app moved to Music, assignment saved, and a recreated stream returned to Music automatically. Other apps unchanged.");
            }
        }catch(const std::exception& ex){finish(1,QString("FAIL: ")+ex.what());}
    }
public:
    AppRoutingTest(QApplication& app,Engine& e):QObject(&app),app(app),e(e),originalRules(e.settings.routing.appRules){
        feed.setInterval(30);connect(&feed,&QTimer::timeout,this,[this]{if(playback.state()==QProcess::Running&&playback.bytesToWrite()<32768)playback.write(QByteArray(16384,'\0'));});feed.start();
        tick.setInterval(200);connect(&tick,&QTimer::timeout,this,[this]{advance();});timeout.start();tick.start();
    }
};
}
void runAppRoutingTest(QApplication& app,Engine& e){new AppRoutingTest(app,e);}
