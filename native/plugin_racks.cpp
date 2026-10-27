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
#include <QtCore/QSaveFile>
#include <QtCore/QUuid>
#include <QtCore/QEventLoop>
#include <QtWidgets/QApplication>
#include <QtWidgets/QProgressDialog>
QString pluginNodeName(int i){return QString("linux_mixer.plugins.input%1").arg(i);}
QString PluginRacks::folder(int i)const{return stateRoot()+QString("/plugin-racks/input%1").arg(i);}
QString PluginRacks::executable()const{const QString p=QCoreApplication::applicationDirPath()+"/linux-mixer-plugin-host";return QFileInfo::exists(p)?p:QString{};}
PluginRacks::PluginRacks(Engine& e):QObject(&e),engine(e){
    watchdog.setInterval(1000);
    connect(&watchdog,&QTimer::timeout,this,[this]{
        if(closing||!active||profilePaused)return;
        for(int i=0;i<8;++i){
            if(hosts[i].state()!=QProcess::Running||failed[i]||!progress[i].isValid())continue;
            QFile file(folder(i)+"/heartbeat.json");
            if(file.open(QIODevice::ReadOnly)){
                const auto bytes=file.read(4096);const auto obj=QJsonDocument::fromJson(bytes).object();
                if(obj.value("pid").toInteger()==hosts[i].processId()&&bytes!=heartbeat[i]){heartbeat[i]=bytes;progress[i].restart();}
            }
            // Allow a longer first load (especially Wine), but never leave a hung
            // GUI or audio callback inserted indefinitely. No automatic crash loop.
            QFile busyFile(folder(i)+"/busy.json");bool busy=false;
            if(busyFile.open(QIODevice::ReadOnly))busy=QJsonDocument::fromJson(busyFile.read(4096)).object().value("pid").toInteger()==hosts[i].processId();
            if(progress[i].elapsed()>((heartbeat[i].isEmpty()||busy)?60000:15000)){
                hung[i]=true;failed[i]=true;hosts[i].kill();
            }
        }
    });watchdog.start();
    for(int i=0;i<8;++i){
        connect(&hosts[i],&QProcess::finished,this,[this,i](int code,QProcess::ExitStatus status){
            if(!closing&&!profilePaused&&active&&engine.settings.pluginRacks[i]){failed[i]=true;engine.message=hung[i]?"Plugin rack stopped responding; normal input routing restored. Reopen Plugins to retry.":status==QProcess::CrashExit||code!=0?"Plugin rack crashed; normal input routing restored. Reopen Plugins to retry.":"Plugin rack closed; normal input routing restored";if(engine.changed)engine.changed();}
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
    hung[i]=false;visible[i]=!background;heartbeat[i].clear();progress[i].start();
    QDir().mkpath(folder(i));QFile::remove(folder(i)+"/busy.json");QFile::remove(folder(i)+"/heartbeat.json");QFile::remove(folder(i)+"/hide.request");QFile::remove(folder(i)+"/visibility.json");
    const QString log=folder(i)+"/host.log";if(QFileInfo(log).size()>2*1024*1024){QFile::remove(log+".previous");QFile::rename(log,log+".previous");}QFile::remove(folder(i)+"/quit.request");QFile::remove(folder(i)+"/show.request");
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
    if(i<0||i>7||profilePaused)return;
    if(!active){engine.message="Start the audio engine before opening input plugins";if(engine.changed)engine.changed();return;}
    if(executable().isEmpty()){engine.message="Plugin host is missing; rebuild with BUILD_PLUGIN_HOST=ON";if(engine.changed)engine.changed();return;}
    engine.settings.pluginRacks[i]=true;engine.save();failed[i]=false;
    if(hosts[i].state()!=QProcess::NotRunning){
        const QString show=folder(i)+"/show.request",hide=folder(i)+"/hide.request";
        // Use the worker's actual state after native X / Escape / Hide, but
        // preserve our pending intent for clicks faster than its event timer.
        if(!QFile::exists(show)&&!QFile::exists(hide)){
            QFile state(folder(i)+"/visibility.json");if(state.open(QIODevice::ReadOnly)){
                const auto value=QJsonDocument::fromJson(state.read(4096)).object();
                if(value.value("pid").toInteger()==hosts[i].processId())visible[i]=value.value("visible").toBool();
            }
        }
        visible[i]=!visible[i];QFile::remove(visible[i]?hide:show);
        QFile request(visible[i]?show:hide);if(request.open(QIODevice::WriteOnly))request.write(visible[i]?"show":"hide");
    }
    else launch(i,false);
}
void PluginRacks::sync(bool enabled){
    active=enabled;if(closing||profilePaused)return;
    for(int i=0;i<8;++i){if(active&&engine.settings.pluginRacks[i]&&!failed[i])launch(i,true);else if(hosts[i].state()!=QProcess::NotRunning){QFile f(folder(i)+"/quit.request");if(f.open(QIODevice::WriteOnly))f.write("quit");}}
}
void PluginRacks::shutdown(){
    if(closing)return;
    closing=true;active=false;watchdog.stop();
    for(int i=0;i<8;++i)if(hosts[i].state()!=QProcess::NotRunning){QFile f(folder(i)+"/quit.request");if(f.open(QIODevice::WriteOnly))f.write("quit");}
    for(auto& host:hosts)if(host.state()!=QProcess::NotRunning&&!host.waitForFinished(1500)){host.terminate();if(!host.waitForFinished(500)){host.kill();host.waitForFinished(500);}}
}

namespace {
bool profileWait(const QString& label,const std::function<bool()>& completed,QString& error){
    QEventLoop loop;QElapsedTimer deadline;deadline.start();bool done=false;
    QProgressDialog progress(label,"Cancel",0,0,QApplication::activeWindow());progress.setWindowModality(Qt::ApplicationModal);progress.setMinimumDuration(300);
    QTimer poll;poll.setInterval(50);
    QObject::connect(&poll,&QTimer::timeout,&loop,[&]{
        if(completed()){done=error.isEmpty();loop.quit();}
        else if(deadline.elapsed()>15000){error="Plugin operation timed out; no profile was applied";loop.quit();}
    });
    QObject::connect(&progress,&QProgressDialog::canceled,&loop,[&]{error="Profile operation canceled";loop.quit();});
    poll.start();loop.exec();return done;
}
}
bool PluginRacks::snapshot(QString& error){
    error.clear();const QString token=QUuid::createUuid().toString(QUuid::Id128);QList<int> pending;
    for(int i=0;i<8;++i)if(hosts[i].state()!=QProcess::NotRunning){
        pending.append(i);QSaveFile request(folder(i)+"/save.request");
        if(!request.open(QIODevice::WriteOnly)||request.write(token.toUtf8())<0||!request.commit()){error="Cannot request a fresh plugin snapshot";return false;}
    }
    if(pending.isEmpty())return true;
    return profileWait("Saving current plugin states…",[&]{
        bool all=true;
        for(int i:pending){
            if(hosts[i].state()==QProcess::NotRunning){error="Plugin rack closed before its state was saved";return true;}
            QFile reply(folder(i)+"/save.reply.json");
            if(!reply.open(QIODevice::ReadOnly)){all=false;continue;}
            const auto ack=QJsonDocument::fromJson(reply.read(4096)).object();
            if(ack.value("token").toString()!=token||ack.value("pid").toInteger()!=hosts[i].processId()){all=false;continue;}
            if(!ack.value("ok").toBool()){error="Plugin rack could not save its state";return true;}
        }return all;
    },error);
}
bool PluginRacks::pauseForProfile(QString& error){
    error.clear();profilePaused=true;
    for(int i=0;i<8;++i)if(hosts[i].state()!=QProcess::NotRunning){QSaveFile request(folder(i)+"/quit.request");if(!request.open(QIODevice::WriteOnly)||request.write("quit")<0||!request.commit()){error="Cannot pause plugin rack for restore";resumeAfterProfile();return false;}}
    bool idle=true;for(auto& host:hosts)idle=idle&&host.state()==QProcess::NotRunning;
    if(idle)return true;
    const bool ok=profileWait("Pausing plugin racks for restore…",[&]{for(auto& host:hosts)if(host.state()!=QProcess::NotRunning)return false;return true;},error);
    if(!ok){for(int i=0;i<8;++i)QFile::remove(folder(i)+"/quit.request");resumeAfterProfile();}return ok;
}
void PluginRacks::resumeAfterProfile(){profilePaused=false;failed.fill(false);hung.fill(false);sync(active);}
