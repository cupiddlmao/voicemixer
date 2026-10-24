#include "plugin_racks.h"
#include "engine.h"
#include "paths.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTimer>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
QString pluginNodeName(int i){return QString("linux_mixer.plugins.input%1").arg(i);}
QString PluginRacks::folder(int i)const{return stateRoot()+QString("/plugin-racks/input%1").arg(i);}
QString PluginRacks::executable()const{const QString p=QCoreApplication::applicationDirPath()+"/linux-mixer-plugin-host";return QFileInfo::exists(p)?p:QString{};}
PluginRacks::PluginRacks(Engine& e):QObject(&e),engine(e){
    for(int i=0;i<8;++i){
        connect(&hosts[i],&QProcess::finished,this,[this,i](int code,QProcess::ExitStatus status){
            if(!closing&&active&&engine.settings.pluginRacks[i]){failed[i]=true;engine.message=status==QProcess::CrashExit||code!=0?"Plugin rack crashed; normal input routing restored. Reopen Plugins to retry.":"Plugin rack closed; normal input routing restored";if(engine.changed)engine.changed();}
            QTimer::singleShot(0,&engine,[this]{if(!closing)engine.refresh();});
        });
        connect(&hosts[i],&QProcess::errorOccurred,this,[this,i](QProcess::ProcessError error){if(error==QProcess::FailedToStart){failed[i]=true;engine.message="Cannot start plugin host: "+hosts[i].errorString();if(engine.changed)engine.changed();}});
    }
}
PluginRacks::~PluginRacks(){shutdown();}
bool PluginRacks::running(int i)const{return active&&engine.settings.pluginRacks[i]&&!failed[i]&&hosts[i].state()==QProcess::Running;}
void PluginRacks::launch(int i,bool background){
    if(hosts[i].state()!=QProcess::NotRunning)return;
    const auto exe=executable();if(exe.isEmpty()){failed[i]=true;engine.message="Plugin hosting was not built. Rebuild with BUILD_PLUGIN_HOST=ON.";if(engine.changed)engine.changed();return;}
    QDir().mkpath(folder(i));QFile::remove(folder(i)+"/quit.request");QFile::remove(folder(i)+"/show.request");
    QStringList args{folder(i),pluginNodeName(i),i<5?QString("Hardware input %1").arg(i+1):QStringList{"System","Music","AUX"}[i-5]};if(background)args<<"--background";
    auto environment=QProcessEnvironment::systemEnvironment();
    QString bridge=QDir::homePath()+"/.local/share/yabridge";
    QFile runtimeFile(stateRoot()+"/plugin-runtime.json");
    if(runtimeFile.open(QIODevice::ReadOnly)){
        const auto config=QJsonDocument::fromJson(runtimeFile.readAll()).object();
        const QString bin=config.value("binDirectory").toString();
        if(QFileInfo::exists(bin+"/wine"))environment.insert("PATH",bin+":"+environment.value("PATH"));
        const QString configuredBridge=config.value("bridgeDirectory").toString();
        if(QFileInfo::exists(configuredBridge+"/yabridge-host.exe"))bridge=configuredBridge;
        for(const auto& key:QStringList{"WINELOADER","WINESERVER"}){const auto path=config.value(key).toString();if(QFileInfo::exists(path))environment.insert(key,path);}
    }
    if(QFileInfo::exists(bridge+"/yabridge-host.exe"))environment.insert("PATH",bridge+":"+environment.value("PATH"));
    hosts[i].setProcessEnvironment(environment);
    hosts[i].setStandardOutputFile(folder(i)+"/host.log",QIODevice::Append);hosts[i].setStandardErrorFile(folder(i)+"/host.log",QIODevice::Append);hosts[i].start(exe,args);
    QTimer::singleShot(600,&engine,[this]{if(!closing)engine.refresh();});
}
void PluginRacks::open(int i){
    if(i<0||i>7)return;
    if(!active){engine.message="Start the audio engine before opening input plugins";if(engine.changed)engine.changed();return;}
    if(executable().isEmpty()){engine.message="Plugin host is missing; rebuild with BUILD_PLUGIN_HOST=ON";if(engine.changed)engine.changed();return;}
    engine.settings.pluginRacks[i]=true;engine.save();failed[i]=false;
    if(hosts[i].state()!=QProcess::NotRunning){QFile f(folder(i)+"/show.request");if(f.open(QIODevice::WriteOnly))f.write("show");}
    else launch(i,false);
}
void PluginRacks::sync(bool enabled){
    active=enabled;if(closing)return;
    for(int i=0;i<8;++i){if(active&&engine.settings.pluginRacks[i]&&!failed[i])launch(i,true);else if(hosts[i].state()!=QProcess::NotRunning){QFile f(folder(i)+"/quit.request");if(f.open(QIODevice::WriteOnly))f.write("quit");}}
}
void PluginRacks::shutdown(){
    if(closing)return;
    closing=true;active=false;
    for(int i=0;i<8;++i)if(hosts[i].state()!=QProcess::NotRunning){QFile f(folder(i)+"/quit.request");if(f.open(QIODevice::WriteOnly))f.write("quit");}
    for(auto& host:hosts)if(host.state()!=QProcess::NotRunning&&!host.waitForFinished(1500)){host.terminate();if(!host.waitForFinished(500)){host.kill();host.waitForFinished(500);}}
}
