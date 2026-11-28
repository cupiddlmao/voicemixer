// End-to-end check: actual window events -> live PipeWire controls -> muted audio.
#include "engine.h"
#include "console.h"
#include "routing.h"
#include <QtWidgets/QApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QContextMenuEvent>
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
            require(e.running,"the smoke test needs a live engine: re-run with --smoke-test --start-engine");
            require(e.matrix->ready,"routing matrix is not ready");
            *original=e.settings;
            *nodeId=captureNode().value("id").toInt();
            // Gate knob on Input 1: centre of {x+84,y+232} = (100,284).
            QWheelEvent wheel({100,284},{100,284},{},{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(&c,&wheel);
            click(c,{74.5,178}); // pad: colour pad centre, padInner(0) centre.
            click(c,{48.5,323}); // denoise: Denoise chip {x+8,y+260,49,22}, centre.
            click(c,{107.5,575}); // route-b1: Input 1 route row 5 {x+74,564,35,22}.
            // B1 master fader: centre of {xx+14,514,30,130}, xx=1058+5*50.
            QWheelEvent busWheel({1337,579},{1337,579},{},{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(&c,&busWheel);
            // System virtual fader: centre of {x+34,gainTop-8,40,gainH+16}, x=661.
            QWheelEvent systemWheel({715,539},{715,539},{},{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(&c,&systemWheel);
            // B2 master fader: xx=1058+6*50, centre (1387,579).
            QWheelEvent b2Wheel({1387,579},{1387,579},{},{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(&c,&b2Wheel);
            click(c,{874.5,654}); // music-mute: Music strip Mute {x+60,y+588,49,28}, centre.
            click(c,{107.5,599}); // route-b2: Input 1 route row 6 {x+74,588,35,22}.
            click(c,{752.5,623}); // route-b3: System strip route row 7 {x+74,612,35,22}.
            // System EQ slot 0 (left Treble): eqX(5,0)=692 on the y+114 row.
            QWheelEvent eqWheel({692,166},{692,166},{},{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(&c,&eqWheel);
            // Input 1 Reverb send: tripleX(0,0)=39 on the y+350 row.
            QWheelEvent sendWheel({39,402},{39,402},{},{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(&c,&sendWheel);
            // B2 balance knob: centre {xx+23,busTop+154}, xx=1358.
            QWheelEvent balanceWheel({1381,480},{1381,480},{},{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(&c,&balanceWheel);
            click(c,{1381,399}); // mono-b2: B2 downmix {xx,busTop+62,46,22}, centre.
            if(!e.settings.mute)click(c,{100.5,654}); // mic-mute: Input 1 Mute, centre.
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

// Actual pointer events and live mixer parameters, with settings restored afterward.
void runPositionTest(QApplication& app,Engine& e,Console& c){
    auto* state=new QObject(&app);auto* original=new Settings(e.settings);
    QTimer::singleShot(900,state,[&,state,original]{
        try{
            require(e.matrix->ready,"routing matrix is not ready");
            for(int i=0;i<5;++i){
                // padInner(i) centre is x=74.5; QPoint is integral, so 74.
                const QPoint pos(74+i*129,190);QContextMenuEvent right(QContextMenuEvent::Mouse,pos,pos);
                const bool previous=e.settings.hardware(i).positionPanel;QApplication::sendEvent(&c,&right);
                require(e.settings.hardware(i).positionPanel!=previous,"right-click did not switch this panel");
                QApplication::sendEvent(&c,&right);require(e.settings.hardware(i).positionPanel==previous,"second right-click did not restore panel");
            }
            e.settings.hardware(0).positionPanel=false;c.repaint();
            QContextMenuEvent right(QContextMenuEvent::Mouse,{74,190},{74,190});QApplication::sendEvent(&c,&right);
            require(e.settings.hardware(0).positionPanel,"position mode did not activate");
            const double colorX=e.settings.colorX,colorY=e.settings.colorY;
            // The pad's depth zero is its visual centre, y=178 (inner box 148..208).
            // Before the redesign the zero sat at y=186 in a box centred on 182, so 189
            // read as dead-zero; with a centred pad it would read depth 0.367 and weaken
            // the pan the assertions below check.
            click(c,{24,178}); // pan-left: x=24 is left of padInner(0), so pan clamps to -1.
            near(e.settings.routing.hardwarePan[0],-1,"position pad did not pan left");
            near(e.settings.colorX,colorX,"panning changed color warmth");near(e.settings.colorY,colorY,"panning changed color presence");
            Settings loaded;loaded.load(e.settings.json());require(loaded.positionPanel,"panel mode was not serialized");near(loaded.routing.hardwarePan[0],-1,"pan was not serialized");
            Settings legacy;legacy.load(QJsonObject{});require(!legacy.positionPanel,"legacy profile should open color mode");near(legacy.routing.hardwarePan[0],0,"legacy profile should center pan");
            const int id=captureNode(busCaptureName(5)).value("id").toInt();
            QTimer::singleShot(900,state,[&,state,original,id]{
                int result=0;
                try{
                    const auto node=captureNode(busCaptureName(5));require(node.value("id").toInt()==id,"panning recreated the audio graph");
                    if(!e.settings.routing.busComposite[5]){
                        near(parameter(node,"left:Gain 1"),e.settings.routing.routes[0][5]?1:0,"left pan did not reach DSP");
                        near(parameter(node,"right:Gain 1"),0,"left pan did not silence right route");
                    }
                    // Centre of padInner(0): 21.5 + 74/2 = 58.5 from the strip edge.
                    QMouseEvent reset(QEvent::MouseButtonDblClick,{74.5,178},{74.5,178},Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&c,&reset); // pan-reset: pad centre
                    near(e.settings.routing.hardwarePan[0],0,"double-click did not center position");
                    click(c,{116,178}); // pan-right: x=116 is past padInner(0), pan clamps to +1.
                    near(e.settings.routing.hardwarePan[0],1,"position pad did not pan right");
                    QContextMenuEvent right(QContextMenuEvent::Mouse,{74,190},{74,190});QApplication::sendEvent(&c,&right);
                    require(!e.settings.positionPanel,"right-click did not return to color mode");
                    near(e.settings.colorX,original->colorX,"color settings were lost while switching modes");
                    QTextStream(stdout)<<"PASS: all five panel toggles, left/right pointer input, center reset, saved mode/pan, legacy defaults and live DSP routing without graph restart.\n";
                }catch(const std::exception& ex){QTextStream(stderr)<<"FAIL: "<<ex.what()<<'\n';result=1;}
                e.settings=*original;e.apply();QTimer::singleShot(700,state,[&,original,result]{delete original;app.exit(result);});
            });
        }catch(const std::exception& ex){e.settings=*original;e.apply();QTextStream(stderr)<<"FAIL: "<<ex.what()<<'\n';delete original;app.exit(1);}
    });
}

// Keep pointer events arriving faster than the control interval. Read PipeWire
// asynchronously so the test cannot accidentally let a debounce timer expire.
void runLiveControlsTest(QApplication& app,Engine& e,Console& c){
    auto* state=new QObject(&app);auto original=std::make_shared<Settings>(e.settings);
    auto finish=[&,state,original](int code){
        e.settings=*original;e.apply();e.save();
        QTimer::singleShot(900,state,[&,code]{app.exit(code);});
    };
    QTimer::singleShot(1000,state,[&,state,original,finish]{
        int bus=-1;
        for(int i=1;i<5;++i)if(e.settings.routing.outputs[i].isEmpty()){bus=i;break;}
        if(bus<0||!e.matrix->ready){QTextStream(stderr)<<"FAIL: need a ready matrix and an unassigned A2–A5 bus\n";finish(1);return;}
        // Check gesture handling on every knob without sending these temporary
        // values to connected outputs; restore before returning to the event loop.
        try{
            for(int i=0;i<8;++i){
                e.settings.routing.busComposite[i]=false;e.settings.routing.busBalance[i]=0;c.repaint();
                // Centre of balance knob i: xx+23 with xx=1058+50i, on the
                // busTop+154 row. Was 1074+52i when the knobs sat 26px higher.
                QPointF p(1081+i*50,480);
                QMouseEvent down(QEvent::MouseButtonPress,p,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&c,&down);
                QMouseEvent move(QEvent::MouseMove,p+QPointF(0,-20),p+QPointF(0,-20),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&c,&move);
                near(e.settings.routing.busBalance[i],.3,"vertical balance gesture failed");
                QMouseEvent up(QEvent::MouseButtonRelease,p,p,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&c,&up);
                QMouseEvent reset(QEvent::MouseButtonDblClick,p,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&c,&reset);
                near(e.settings.routing.busBalance[i],0,"balance reset failed");
            }
        }catch(const std::exception& ex){QTextStream(stderr)<<"FAIL: "<<ex.what()<<'\n';finish(1);return;}
        e.settings=*original;
        auto& r=e.settings.routing;r.busGain[bus]=0;r.busMute[bus]=false;r.busComposite[bus]=false;r.busBalance[bus]=0;r.routes[0][bus]=true;r.hardwarePan[0]=0;
        e.settings.hardware(0).positionPanel=true;e.apply();c.repaint();
        QTimer::singleShot(900,state,[&,state,bus,finish]{
            const QString name=busCaptureName(bus);
            const int nodeId=captureNode(name).value("id").toInt();
            auto phase=std::make_shared<int>(0);
            auto run=std::make_shared<std::function<void()>>();
            *run=[&,state,bus,name,nodeId,phase,run,finish]{
                // Phase 0 presses bus `bus`'s balance knob and drags 30px right
                // (0.45 -> gainL .55); phase 1 presses the position pad and
                // drags to x=93, which is 75% across padInner(0): pan 0.5.
                const QPointF p=*phase==0?QPointF(1081+bus*50,480):QPointF(74.5,178);
                const QPointF target=*phase==0?p+QPointF(30,0):QPointF(93,178);
                QMouseEvent down(QEvent::MouseButtonPress,p,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&c,&down);
                auto* moves=new QTimer(state);moves->setInterval(8);
                QObject::connect(moves,&QTimer::timeout,state,[&,target]{QMouseEvent move(QEvent::MouseMove,target,target,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&c,&move);});moves->start();
                QTimer::singleShot(600,state,[&,state,moves,target,bus,name,nodeId,phase,run,finish]{
                    auto* query=new QProcess(state);
                    QObject::connect(query,&QProcess::finished,state,[&,state,query,moves,target,bus,name,nodeId,phase,run,finish](int code,QProcess::ExitStatus){
                        int result=0;
                        try{
                            require(code==0&&moves->isActive(),"live probe failed");
                            QJsonObject node;
                            for(const auto& item:QJsonDocument::fromJson(query->readAllStandardOutput()).array()){const auto n=item.toObject();if(n.value("info").toObject().value("props").toObject().value("node.name")==name)node=n;}
                            require(node.value("id").toInt()==nodeId,"live adjustment recreated bus");
                            if(*phase==0){near(parameter(node,"gainL:Mult"),.55,"balance did not update while dragging");near(parameter(node,"gainR:Mult"),1,"unexpected right master level");}
                            else{near(parameter(node,"left:Gain 1"),.5,"position did not update while dragging");near(parameter(node,"right:Gain 1"),1,"unexpected right route level");}
                        }catch(const std::exception& ex){QTextStream(stderr)<<"FAIL: "<<ex.what()<<'\n';result=1;}
                        moves->stop();moves->deleteLater();query->deleteLater();
                        QMouseEvent up(QEvent::MouseButtonRelease,target,target,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&c,&up);
                        if(result){*run={};finish(1);return;}
                        if((*phase)++==0){e.settings.routing.busBalance[bus]=0;e.apply();QTimer::singleShot(500,state,*run);}
                        else{QTextStream(stdout)<<"PASS: all eight balance knobs accept vertical drag/reset; master balance and hardware position reach live DSP while mouse events continue every 8 ms, before release; bus node unchanged.\n";*run={};finish(0);}
                    });query->start("pw-dump");
                });
            };(*run)();
        });
    });
}

#include "plugin_racks.h"
#include "paths.h"
#include <QtCore/QDir>
#include <csignal>
void runPluginRoutingTest(QApplication& app,Engine& e,Console&){
    auto* state=new QObject(&app);auto original=std::make_shared<Settings>(e.settings);
    const QString folder=stateRoot()+"/plugin-racks/input7";
    auto restore=[&,state,original,folder](int result){e.settings=*original;e.plugins->sync(e.running);e.save();e.refresh();QTimer::singleShot(800,state,[&,result,folder]{QDir(folder).removeRecursively();app.exit(result);});};
    auto check=[](bool inserted){
        QProcess p;p.start("pw-dump");require(p.waitForFinished(3000)&&p.exitCode()==0,"could not read plugin routing");const auto graph=QJsonDocument::fromJson(p.readAllStandardOutput()).array();
        QHash<int,QString> names;QHash<int,int> portNodes;QHash<int,QString> portChannels;QHash<int,QString> direction;
        for(const auto& v:graph){const auto o=v.toObject(),info=o.value("info").toObject(),props=info.value("props").toObject();int id=o.value("id").toInt();if(o.value("type")=="PipeWire:Interface:Node")names[id]=props.value("node.name").toString();if(o.value("type")=="PipeWire:Interface:Port"){portNodes[id]=props.value("node.id").toInt();portChannels[id]=props.value("audio.channel").toString();direction[id]=info.value("direction").toString();}}
        for(int bus=0;bus<8;++bus)for(int side=0;side<2;++side){int count=0;QString source;for(const auto& v:graph){const auto o=v.toObject(),info=o.value("info").toObject();if(o.value("type")!="PipeWire:Interface:Link")continue;const int in=info.value("input-port-id").toInt(),out=info.value("output-port-id").toInt();if(names.value(portNodes.value(in))==busCaptureName(bus)&&portChannels.value(in)==QString("AUX%1").arg(14+side)){++count;source=names.value(portNodes.value(out));}}require(count==1,"input has missing or duplicate dry/plugin links");require(source==(inserted?pluginNodeName(7):QString("linux_mixer.input2.output")),"input was not switched to expected dry/plugin source");}
    };
    QTimer::singleShot(1000,state,[&,state,original,folder,restore,check]{
        try{require(e.matrix->ready,"matrix not ready");require(!original->pluginRacks[7]&&!QDir(folder).exists(),"AUX rack already configured; refusing to alter it");for(int b=0;b<8;++b)require(!e.settings.routing.busComposite[b],"test needs normal stereo buses");e.settings.pluginRacks[7]=true;e.plugins->sync(true);}
        catch(const std::exception& ex){QTextStream(stderr)<<"FAIL: "<<ex.what()<<'\n';app.exit(1);return;}
        QTimer::singleShot(2200,state,[&,state,restore,check]{
            try{e.refresh();require(e.plugins->running(7),"plugin host did not start");check(true);const auto pid=e.plugins->processId(7);require(pid>1,"plugin process ID missing");::kill(pid,SIGKILL);}
            catch(const std::exception& ex){QTextStream(stderr)<<"FAIL: "<<ex.what()<<'\n';restore(1);return;}
            QTimer::singleShot(2000,state,[&,restore,check]{int result=0;try{check(false);require(e.plugins->hasFailed(7),"host crash was not reported");QTextStream(stdout)<<"PASS: AUX rack connects stereo to all eight buses without duplicate dry paths; killing only the plugin host restores every original input link and leaves the controller running.\n";}catch(const std::exception& ex){QTextStream(stderr)<<"FAIL: "<<ex.what()<<'\n';result=1;}restore(result);});
        });
    });
}
