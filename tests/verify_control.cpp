// Command parsing, settings changes and the on-disk command queue.
#include "control_commands.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextStream>
#include <cmath>
#include <cstdlib>

#define CHECK(x) do{if(!(x)){QTextStream(stderr)<<"FAIL line "<<__LINE__<<": "#x"\n";return 1;}}while(0)

static bool run(Settings& s,const QString& line,QString* error=nullptr){
    ControlCommand c;QString e;const bool ok=parseControlCommand(line.split(' '),c,e)&&applyControlCommand(s,c,e);
    if(error)*error=e;
    return ok;
}

int runControlTests(){
    Settings s;
    CHECK(run(s,"mute mic1"));CHECK(s.hardware(0).mute);
    CHECK(run(s,"mute mic1 toggle"));CHECK(!s.hardware(0).mute);
    CHECK(run(s,"mute MIC5 on"));CHECK(s.hardware(4).mute);
    CHECK(run(s,"mute mic5 off"));CHECK(!s.hardware(4).mute);
    CHECK(run(s,"gain mic2 -6.5"));CHECK(s.hardware(1).gain==-6.5);
    CHECK(run(s,"gain mic2 99"));CHECK(s.hardware(1).gain==12);
    CHECK(run(s,"gain mic2 -999"));CHECK(s.hardware(1).gain==-60);
    CHECK(run(s,"bypass mic3 on"));CHECK(s.hardware(2).bypass);
    const bool denoise=s.hardware(3).denoise;CHECK(run(s,"denoise mic4"));CHECK(s.hardware(3).denoise!=denoise);
    CHECK(run(s,"mute music on"));CHECK(s.routing.inputMute[1]);
    CHECK(run(s,"gain aux -3"));CHECK(s.routing.inputGain[2]==-3);
    CHECK(run(s,"mute a1 on"));CHECK(s.routing.busMute[0]);
    CHECK(run(s,"gain b1 -4"));CHECK(s.routing.busGain[5]==-4);
    CHECK(run(s,"mute b3 on"));CHECK(s.routing.busMute[7]);
    // Front/back position cue: neutral by default, quieter and wetter toward the back, persisted, applied only to dry sends.
    Settings depth;CHECK(depth.routing.depthGain(0)==1.);
    depth.routing.hardwareDepth[0]=1;depth.routing.hardwareDepth[1]=-1;depth.routing.hardwarePan[0]=-1;
    CHECK(std::abs(depth.routing.depthGain(0)-std::pow(10.,-9/20.))<1e-9);CHECK(std::abs(depth.routing.depthGain(1)-std::pow(10.,3/20.))<1e-9);
    CHECK(depth.routing.depthGain(5)==1.&&depth.routing.depthGain(-1)==1.);
    CHECK(std::abs(depth.routing.dryGain(0,0)-std::pow(10.,-9/20.))<1e-9);CHECK(depth.routing.dryGain(0,1)==0.);CHECK(depth.routing.panGain(0,0)==1.);
    Settings reloaded;reloaded.load(depth.json());CHECK(reloaded.routing.hardwareDepth[0]==1&&reloaded.routing.hardwareDepth[1]==-1);
    Settings legacy;legacy.load(QJsonObject{});CHECK(legacy.routing.hardwareDepth[0]==0);
    QString error;
    for(const char* bad:{"","mute","mute mic6","mute mic0","mute a6","mute b4","mute nowhere","gain mic1","gain mic1 loud","gain mic1 nan","bypass aux","mute mic1 maybe","mute mic1 on extra","explode mic1"})
        CHECK(!run(s,bad,&error));

    QTemporaryDir dir;CHECK(dir.isValid());
    CHECK(sendControlCommand({"mute","mic1","on"},false,dir.path())==1);
    CHECK(sendControlCommand({"mute","mic9"},true,dir.path())==2);
    CHECK(!QDir(dir.path()+"/ctl").exists());
    Settings queued;
    CHECK(sendControlCommand({"mute","mic1","on"},true,dir.path())==0);
    CHECK(sendControlCommand({"gain","mic1","-10"},true,dir.path())==0);
    QStringList errors;CHECK(drainControlCommands(dir.path(),queued,&errors)==2);
    CHECK(errors.isEmpty());CHECK(queued.hardware(0).mute&&queued.hardware(0).gain==-10);
    CHECK(QDir(dir.path()+"/ctl").entryList(QDir::Files).isEmpty());
    CHECK(drainControlCommands(dir.path(),queued)==0);
    QTextStream(stdout)<<"PASS: control command parsing, clamping, rejection of bad input and the command queue.\n";
    return 0;
}
