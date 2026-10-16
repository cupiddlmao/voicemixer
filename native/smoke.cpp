// End-to-end check: actual window events -> live PipeWire controls -> muted audio.
#include "engine.h"
#include "console.h"
#include "routing.h"
#include <QtWidgets/QApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTextStream>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {
QJsonObject captureNode(const QString& name="linux_mixer.capture") {
    QProcess p;p.start("pw-dump");
    if(!p.waitForFinished(3000) || p.exitCode()!=0)throw std::runtime_error("pw-dump failed");
    for(const auto& item:QJsonDocument::fromJson(p.readAllStandardOutput()).array()) {
        const auto n=item.toObject();
        if(n.value("info").toObject().value("props").toObject().value("node.name")==name)return n;
    }
    throw std::runtime_error("capture node missing");
}
double parameter(const QJsonObject& node,const QString& key) {
    for(const auto& props:node.value("info").toObject().value("params").toObject().value("Props").toArray()) {
        const auto a=props.toObject().value("params").toArray();
        for(int i=0;i+1<a.size();i+=2)if(a[i].toString()==key)return a[i+1].toDouble();
    }
    throw std::runtime_error(("missing parameter: "+key).toStdString());
}
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void near(double actual,double expected,const char* message){require(std::abs(actual-expected)<.001,message);}
void click(Console& c,QPointF point) {
    QMouseEvent down(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&c,&down);
    QMouseEvent up(QEvent::MouseButtonRelease,point,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(&c,&up);
}
}
void runSmoke(QApplication& app,Engine& e,Console& c) {
    auto* state=new QObject(&app);
    auto* original=new Settings;
    auto* nodeId=new int(-1);
    QTimer::singleShot(700,state,[&,original,nodeId,state]{
        try {
            require(e.running,"start the native engine before smoke testing");
            require(e.matrix->ready,"routing matrix is not ready");
            *original=e.settings;
            *nodeId=captureNode().value("id").toInt();
            QWheelEvent wheel({99,286},{99,286},{},{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(&c,&wheel);
            click(c,{84,175}); // Actual color pad.
            click(c,{32,355}); // Actual RNNoise button.
            click(c,{99,584}); // B1 route, independent of the A1 monitor path.
            QWheelEvent busWheel({1343,550},{1343,550},{},{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(&c,&busWheel);
            QWheelEvent systemWheel({711,550},{711,550},{},{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(&c,&systemWheel);
            QWheelEvent b2Wheel({1395,550},{1395,550},{},{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(&c,&b2Wheel);
            click(c,{873,662}); // Music strip mute.
            click(c,{99,606}); // Microphone -> B2.
            click(c,{753,628}); // System -> B3.
            QWheelEvent eqWheel({681,155},{681,155},{},{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(&c,&eqWheel);
            QWheelEvent sendWheel({33,415},{33,415},{},{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(&c,&sendWheel);
            QWheelEvent balanceWheel({1386,409},{1386,409},{},{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(&c,&balanceWheel);
            click(c,{1386,450}); // B2 stereo/mono switch.
            if(!e.settings.mute)click(c,{99,662});
            e.settings.routing.busMute[5]=true;e.apply();
        } catch(const std::exception& ex){QTextStream(stderr)<<"FAIL: "<<ex.what()<<'\n';app.exit(1);return;}
        QTimer::singleShot(700,state,[&,original,nodeId,state]{
            int status=0;
            try {
                const auto node=captureNode();
                require(node.value("id").toInt()==*nodeId,"controls recreated the virtual mic");
                near(parameter(node,"gate:Threshold"),std::min(0.,original->threshold+1),"gate wheel did not reach DSP");
                near(parameter(node,"warmth:Gain"),(.5-e.settings.colorX)*12,"color pad did not reach DSP");
                near(parameter(node,"presence:Gain"),(.5-e.settings.colorY)*10,"presence did not reach DSP");
                near(parameter(node,"clean:Gain 2"),original->denoise?0:1,"denoise toggle did not reach DSP");
                near(parameter(node,"fader:Mult"),0,"mute did not reach DSP");
                const auto bus=captureNode(busCaptureName(5));
                near(parameter(bus,"left:Gain 1"),e.settings.routing.routes[0][5]?1.:0.,"B1 microphone route did not reach output bus");
                near(parameter(bus,"gainL:Mult"),0,"B1 master mute did not reach output bus");
                const auto system=captureNode(virtualInputName(0)),music=captureNode(virtualInputName(1));
                near(parameter(system,"left:Mult"),e.settings.routing.inputMute[0]?0:std::pow(10,e.settings.routing.inputGain[0]/20),"System fader did not reach DSP");
                near(parameter(music,"left:Mult"),e.settings.routing.inputMute[1]?0:std::pow(10,e.settings.routing.inputGain[1]/20),"Music mute did not reach DSP");
                near(parameter(system,"leftEq0:Gain"),e.settings.routing.eq[0][0],"EQ wheel did not reach DSP");
                const auto b2=captureNode(busCaptureName(6)),b3=captureNode(busCaptureName(7));
                near(parameter(b2,"left:Gain 1"),e.settings.routing.routes[0][6]?1.:0.,"mic B2 button did not reach DSP");
                near(parameter(b2,"gainL:Mult"),e.settings.routing.busMute[6]?0:std::pow(10,e.settings.routing.busGain[6]/20)*(1-std::max(0.,e.settings.routing.busBalance[6])),"B2 master fader did not reach DSP");
                near(parameter(b3,"left:Gain 6"),e.settings.routing.routes[5][7]?1.:0.,"System B3 button did not reach DSP");
                near(parameter(b2,"verb0:Gain 1"),e.settings.routing.routes[0][6]?e.settings.routing.reverbSend[0]*e.settings.routing.fxAmount[0]:0.,"reverb send did not reach DSP");
                near(parameter(b2,"monoL:Gain 1"),e.settings.routing.busMono[6]?.5:1.,"mono switch did not reach DSP");
                QProcess recorder;recorder.start("pw-record",{"--raw","--target","linux_mixer.microphone","--rate","48000","--channels","1","--format","f32","--latency","256","-"});
                QByteArray audio;QElapsedTimer timeout;timeout.start();
                while(audio.size()<38400 && timeout.elapsed()<3000){recorder.waitForReadyRead(100);audio+=recorder.readAllStandardOutput();}
                recorder.terminate();recorder.waitForFinished(1000);
                require(audio.size()>=19200,"no audio frames delivered");
                // Skip the first half to exclude any pre-mute buffers.
                for(qsizetype i=audio.size()/8*4;i+4<=audio.size();i+=4){float sample;std::memcpy(&sample,audio.constData()+i,4);require(std::isfinite(sample)&&std::abs(sample)<.000001,"muted source contains nonzero or invalid samples");}
                QTextStream(stdout)<<"PASS: microphone controls, virtual-input fader/mute, B1/B2/B3 routing, EQ, FX sends, balance/mono and master controls reached DSP; node ID stayed stable; muted B1 audio is silent.\n";
            } catch(const std::exception& ex){QTextStream(stderr)<<"FAIL: "<<ex.what()<<'\n';status=1;}
            e.settings=*original;e.apply();
            QTimer::singleShot(500,state,[&,original,nodeId,status]{
                try {const auto node=captureNode();near(parameter(node,"gate:Threshold"),original->threshold,"failed to restore gate");near(parameter(node,"fader:Mult"),original->mute?0:std::pow(10,original->gain/20),"failed to restore gain");}
                catch(const std::exception& ex){QTextStream(stderr)<<"FAIL: "<<ex.what()<<'\n';app.exit(1);return;}
                delete original;delete nodeId;app.exit(status);
            });
        });
    });
}
