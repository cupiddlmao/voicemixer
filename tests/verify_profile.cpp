#include "profile_ui.h"
#include "plugin_racks.h"
#include "routing.h"
#include "paths.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtCore/QTemporaryDir>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTextStream>
#include <stdexcept>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
QJsonObject rack(bool bypass=false){return {{"plugins",QJsonArray{QJsonObject{{"description","<PLUGIN name=\"Test\" format=\"VST3\" file=\"/missing/Test.vst3\" uid=\"1\"/>"},{"state","1.AA"},{"bypass",bypass}}}}};}
ProfileBundle bundle(){ProfileBundle b;Settings settings;settings.pluginRacks[0]=true;settings.mic="missing-mic";settings.routing.outputs[0]="missing-output";settings.sink="missing-output";b.settings=settings.json();for(auto& r:b.racks)r={{"plugins",QJsonArray{}}};b.racks[0]=rack();return b;}
QByteArray bytes(const QString& path){QFile f(path);require(f.open(QIODevice::ReadOnly),"cannot read test file");return f.readAll();}
void reviewLater(bool accept,bool& noticed){QTimer::singleShot(0,[accept,&noticed]{
    for(auto* w:QApplication::topLevelWidgets())if(w->objectName()=="profileRestoreReview"){
        auto* d=qobject_cast<QDialog*>(w);auto* label=d->findChild<QLabel*>("profileMissingNotices");noticed=label&&label->text().contains("Missing plugin")&&label->text().contains("Missing input");
        if(auto* output=d->findChild<QComboBox*>("profileDevice5"))output->setCurrentIndex(std::max(0,output->findData("output-b")));
        if(accept)d->accept();else d->reject();return;
    }
});}
bool waitFor(const std::function<bool()>& condition,int milliseconds=12000){QEventLoop loop;QElapsedTimer time;time.start();QTimer timer;timer.setInterval(50);QObject::connect(&timer,&QTimer::timeout,&loop,[&]{if(condition()||time.elapsed()>milliseconds)loop.quit();});timer.start();loop.exec();return condition();}
}
int runProfileTests(){try{
    QString error;ProfileBundle parsed;auto original=bundle();
    require(parseProfile(encodeProfile(original),parsed,error),qPrintable(error));require(parsed.racks==original.racks&&parsed.settings==original.settings,"bundle round-trip changed data");
    for(int version=1;version<=3;++version){auto legacy=original.settings;legacy["version"]=version;require(parseProfile(QJsonDocument(legacy).toJson(),parsed,error)&&parsed.legacy,"legacy preset rejected");}
    auto invalid=[&](ProfileBundle b){require(!parseProfile(encodeProfile(b),parsed,error),"invalid bundle accepted");};
    auto incomplete=original;incomplete.settings.remove("gain");invalid(incomplete);
    auto bad=original;bad.settings["hardware"]=QJsonArray{};invalid(bad);
    bad=original;bad.settings["pluginRacks"]=QString("bad");invalid(bad);
    bad=original;auto a=bad.racks[0]["plugins"].toArray();auto p=a[0].toObject();p["state"]="99999999.AA";a[0]=p;bad.racks[0]["plugins"]=a;invalid(bad);
    p["state"]="1.A";a[0]=p;bad.racks[0]["plugins"]=a;invalid(bad);
    p["state"]="1.AA";p["description"]="<!DOCTYPE PLUGIN [<!ENTITY x 'oops'>]><PLUGIN name=\"&x;\"/>";a[0]=p;bad.racks[0]["plugins"]=a;invalid(bad);
    p["description"]="<PLUGIN name=\"X\" format=\"VST3\" file=\"../../evil.vst3\"/>";a[0]=p;bad.racks[0]["plugins"]=a;invalid(bad);
    require(!parseProfile("{bad",parsed,error),"invalid JSON accepted");
    auto future=QJsonDocument::fromJson(encodeProfile(original)).object();future["bundleVersion"]=2;require(!parseProfile(QJsonDocument(future).toJson(),parsed,error),"future version accepted");
    require(!parseProfile(QByteArray(profileSizeLimit+1,' '),parsed,error),"oversized profile accepted");
    QTemporaryDir dir;require(dir.isValid(),"temporary dir failed");QString backup;
    require(installProfile(dir.path(),original,backup,error),qPrintable(error));
    auto changed=original;changed.racks[0]=rack(true);changed.settings["gain"]=-7;
    require(installProfile(dir.path(),changed,backup,error),qPrintable(error));require(bytes(backup+"/settings.json")==QJsonDocument(original.settings).toJson(),"rollback settings backup missing");require(bytes(backup+"/plugin-racks/input0/rack.json")==QJsonDocument(original.racks[0]).toJson(),"rollback rack backup missing");
    ProfileBundle disk;require(readProfileRacks(dir.path(),disk,error)&&disk.racks==changed.racks,"rack files were not installed");
    auto legacy=changed;legacy.legacy=true;legacy.settings["gain"]=-9;require(installProfile(dir.path(),legacy,backup,error),"legacy install failed");require(bytes(dir.path()+"/plugin-racks/input0/rack.json")==QJsonDocument(changed.racks[0]).toJson(),"legacy preset replaced plugins");
    const auto before=bytes(dir.path()+"/settings.json"),beforeRack=bytes(dir.path()+"/plugin-racks/input0/rack.json");
    const auto blocked=dir.path()+"/plugin-racks/input7";require(QFile::setPermissions(blocked,QFile::ReadOwner|QFile::ExeOwner),"cannot set failure fixture permissions");
    const bool installed=installProfile(dir.path(),original,backup,error);QFile::setPermissions(blocked,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    require(!installed,"write failure not detected");require(bytes(dir.path()+"/settings.json")==before&&bytes(dir.path()+"/plugin-racks/input0/rack.json")==beforeRack,"partial transaction was not rolled back");
    QTemporaryDir outside;QFile::remove(dir.path()+"/plugin-racks/input0/rack.json");require(QFile::link(outside.path(),dir.path()+"/plugin-racks/input0/rack.json"),"cannot create symlink fixture");require(!installProfile(dir.path(),original,backup,error),"symlink destination accepted");
    Settings current;current.mic="mic-a";current.routing.outputs[0]="output-a";
    QList<Device> devices{{"mic-a","Microphone A",true,{"MONO"}},{"output-a","Headphones A",false,{"FL","FR"}},{"output-b","Headphones B",false,{"FL","FR"}}};
    bool noticed=false;auto reviewed=original;reviewLater(true,noticed);require(reviewProfile(reviewed,current,devices),"review did not accept");require(noticed,"missing device/plugin notices absent");Settings result;result.load(reviewed.settings);require(result.mic=="mic-a"&&result.routing.outputs[0]=="output-b","device remapping failed");require(!result.pluginRacks[0]&&reviewed.racks[0]==original.racks[0],"missing plugin state lost or rack enabled");
    reviewed=original;reviewLater(false,noticed);require(!reviewProfile(reviewed,current,devices),"review cancel ignored");
    QTextStream(stdout)<<"PASS: profile round-trip, legacy v1/v2/v3, invalid/oversized/state/XML validation, rollback backups and partial-write recovery, symlink rejection, missing-device/plugin notices, remapping and cancel.\n";return 0;
}catch(const std::exception& e){QTextStream(stderr)<<"FAIL: "<<e.what()<<'\n';return 1;}}

int runProfileFlow(){try{
    require(!qEnvironmentVariableIsEmpty("LINUX_MIXER_STATE_DIR")&&!qEnvironmentVariableIsEmpty("PIPEWIRE_RUNTIME_DIR"),"flow test requires an isolated profile and server");
    Engine engine;QString error;const QString root=stateRoot();
    QTimer dismiss;dismiss.setInterval(30);bool sawError=false;
    QObject::connect(&dismiss,&QTimer::timeout,[&]{for(auto* w:QApplication::topLevelWidgets())if(auto* m=qobject_cast<QMessageBox*>(w)){sawError=true;QTextStream(stderr)<<"Profile dialog: "<<m->text()<<'\n';m->accept();}});dismiss.start();
    require(waitFor([&]{return engine.matrix->ready&&engine.plugins->running(5)&&QFileInfo::exists(root+"/plugin-racks/input5/heartbeat.json");}),"private engine/plugin startup failed");
    const auto oldPid=engine.plugins->processId(5);const QString file=root+"/complete.json";engine.exportPreset(file);require(!sawError&&QFileInfo::exists(file),"fresh-state export failed");
    ProfileBundle exported;require(readProfile(file,exported,error),qPrintable(error));auto plugins=exported.racks[5]["plugins"].toArray();require(plugins.size()==1&&!plugins[0].toObject()["state"].toString().isEmpty(),"export omitted live plugin state");
    Settings saved;saved.load(exported.settings);saved.routing.inputGain[0]=-7.5;saved.routing.routes[5][5]=true;exported.settings=saved.json();auto plugin=plugins[0].toObject();plugin["bypass"]=true;plugins[0]=plugin;exported.racks[5]["plugins"]=plugins;require(writeProfileFile(file,encodeProfile(exported),error),"cannot write flow fixture");
    QTimer accept;accept.setInterval(30);QObject::connect(&accept,&QTimer::timeout,[]{for(auto* w:QApplication::topLevelWidgets())if(w->objectName()=="profileRestoreReview")qobject_cast<QDialog*>(w)->accept();});accept.start();engine.importPreset(file);accept.stop();
    require(!sawError&&engine.settings.routing.inputGain[0]==-7.5&&engine.settings.routing.routes[5][5],"profile settings/routing not restored");
    require(waitFor([&]{return engine.plugins->running(5)&&engine.plugins->processId(5)!=oldPid&&QFileInfo::exists(root+"/plugin-racks/input5/heartbeat.json");}),"restored plugin did not restart");
    ProfileBundle after;require(readProfileRacks(root,after,error)&&after.racks[5]==exported.racks[5],"restored plugin chain/state differs");
    const QString legacyPath=root+"/legacy.json";require(writeProfileFile(legacyPath,QJsonDocument(QJsonObject{{"version",1},{"gain",-2}}).toJson(),error),"legacy fixture failed");
    accept.start();engine.importPreset(legacyPath);accept.stop();require(!sawError&&engine.settings.gain==-2&&engine.settings.pluginRacks[5],"partial legacy restore lost current settings");
    require(waitFor([&]{return engine.plugins->running(5)&&QFileInfo::exists(root+"/plugin-racks/input5/heartbeat.json");}),"legacy restore did not resume rack");
    ProfileBundle legacyRacks;require(readProfileRacks(root,legacyRacks,error)&&legacyRacks.racks[5]==after.racks[5],"legacy restore changed plugin chain/state");
    const auto unchanged=engine.settings.json();const QString invalidPath=root+"/invalid.json";require(writeProfileFile(invalidPath,"[]",error),"invalid fixture failed");engine.importPreset(invalidPath);require(sawError&&engine.settings.json()==unchanged,"invalid restore changed settings");sawError=false;
    const auto before=engine.settings.json();engine.recorder->recording=true;engine.importPreset(file);engine.recorder->recording=false;require(sawError&&engine.settings.json()==before,"active recording did not block restore");
    engine.stop();engine.shutdown();
    const QString missingFolder=root+"/missing-rack";require(QDir().mkpath(missingFolder),"missing rack fixture failed");const auto missingBytes=QJsonDocument(rack()).toJson();require(writeProfileFile(missingFolder+"/rack.json",missingBytes,error),"missing rack file failed");
    QProcess host;host.start(QCoreApplication::applicationDirPath()+"/linux-mixer-plugin-host",{missingFolder,"linux_mixer.test.missing","Missing plugin test","--background"});require(host.waitForStarted(2000),"missing plugin host failed to start");
    require(waitFor([&]{return QFileInfo::exists(missingFolder+"/heartbeat.json");}),"missing plugin host heartbeat absent");QFile request(missingFolder+"/save.request");require(request.open(QIODevice::WriteOnly),"snapshot fixture failed");request.write("missing-test");request.close();
    require(waitFor([&]{return QFileInfo::exists(missingFolder+"/save.reply.json");}),"missing plugin snapshot was not acknowledged");const auto ack=QJsonDocument::fromJson(bytes(missingFolder+"/save.reply.json")).object();require(!ack.value("ok").toBool()&&ack.value("token")=="missing-test","partial plugin chain was saved");
    QFile quit(missingFolder+"/quit.request");require(quit.open(QIODevice::WriteOnly),"quit fixture failed");quit.write("quit");quit.close();require(waitFor([&]{return host.state()==QProcess::NotRunning;}),"missing plugin host did not quit");require(bytes(missingFolder+"/rack.json")==missingBytes,"missing plugin state overwritten during save/shutdown");
    QTextStream(stdout)<<"PASS: private engine fresh-state snapshot IPC, complete export, reviewed restore, plugin restart with saved state/bypass, routing, partial legacy restore, invalid import, active-recording protection and missing-plugin state preservation.\n";return 0;
}catch(const std::exception& e){QTextStream(stderr)<<"FAIL: "<<e.what()<<'\n';return 1;}}
