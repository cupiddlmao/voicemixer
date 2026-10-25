#include "paths.h"
#include "hardware_links.h"
#include "engine.h"
#include "console.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QSystemTrayIcon>
#include <QtWidgets/QMenu>
#include <QtGui/QPainter>
#include <QtCore/QStandardPaths>
#include <QtGui/QIcon>
#include <QtCore/QLockFile>
#include <QtCore/QDir>
#include <QtCore/QTimer>
#include <QtCore/QTextStream>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QFile>
void runHealthUiTest(QApplication&,Console&);
void runRecorderTest(QApplication&);
void runSmoke(QApplication&,Engine&,Console&);
void runPositionTest(QApplication&,Engine&,Console&);
void runPluginRoutingTest(QApplication&,Engine&,Console&);
void runLiveControlsTest(QApplication&,Engine&,Console&);
void runAppRoutingTest(QApplication&,Engine&);

static void setDesktopMaximized(Console& window,bool maximize) {
    if(maximize)window.showMaximized();else window.showNormal();
    if(qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE") || QGuiApplication::platformName()=="offscreen")return;
    // This Hyprland configuration suppresses ordinary Wayland maximize requests.
    // Target only our own mapped surface; never change compositor configuration.
    QTimer::singleShot(450,&window,[&window,maximize]{
        auto* query=new QProcess(&window);
        QObject::connect(query,&QProcess::finished,&window,[&window,query,maximize](int code,QProcess::ExitStatus){
            const auto clients=QJsonDocument::fromJson(query->readAllStandardOutput()).array();query->deleteLater();
            if(code!=0)return;
            for(const auto& item:clients){
                const auto client=item.toObject();
                if(client.value("pid").toInteger()!=QCoreApplication::applicationPid())continue;
                const QString address=client.value("address").toString();
                if(!QRegularExpression("^0x[0-9a-fA-F]+$").match(address).hasMatch())return;
                if(client.value("fullscreen").toInt()==(maximize?1:0) && client.value("fullscreenClient").toInt()==(maximize?1:0))return;
                auto* request=new QProcess(&window);
                QObject::connect(request,&QProcess::finished,request,&QObject::deleteLater);
                const QString script=QString("hl.dispatch(hl.dsp.window.fullscreen_state({internal=%1,client=%1,action='set',window='address:%2'}))").arg(maximize?1:0).arg(address);
                request->start("hyprctl",{"eval",script});return;
            }
        });
        query->start("hyprctl",{"-j","clients"});
    });
}

int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setApplicationName("Linux Mixer");app.setOrganizationName("PersonalAudio");
    app.setApplicationVersion(VOICEMIXER_VERSION);app.setStyle("Fusion");
    const auto args=app.arguments();
    if(args.contains("--version")){QTextStream(stdout)<<"Voicemixer "<<app.applicationVersion()<<"\n";return 0;}
    if(args.contains("--recorder-test")){runRecorderTest(app);return app.exec();}
    if(args.contains("--write-matrix")){
        const int at=args.indexOf("--write-matrix");if(at+1>=args.size())return 2;
        QString prefix="linux_mixer_test";const int pi=args.indexOf("--prefix");if(pi>=0&&pi+1<args.size())prefix=args[pi+1];
        if(!QRegularExpression("^[a-zA-Z0-9_]+$").match(prefix).hasMatch())return 2;
        QFile f(args[at+1]);if(!f.open(QIODevice::WriteOnly))return 1;
        RoutingSettings state;RecorderSettings tape;const int settingsAt=args.indexOf("--settings");if(settingsAt>=0&&settingsAt+1<args.size()){QFile settingsFile(args[settingsAt+1]);if(!settingsFile.open(QIODevice::ReadOnly))return 2;const auto object=QJsonDocument::fromJson(settingsFile.readAll()).object();state.load(object.value("routing").toObject());tape.load(object.value("tape").toObject());}
        f.write(QJsonDocument(matrixConfiguration(state,prefix,&tape)).toJson());return 0;
    }
    QDir().mkpath(stateRoot()+"/.runtime");
    QLockFile lock(stateRoot()+"/.runtime/gui.lock");
    if(!lock.tryLock(100)){const bool quit=args.contains("--quit-running")||args.contains("--stop-running");QFile request(stateRoot()+"/.runtime/"+(quit?"quit.request":"show.request"));if(request.open(QIODevice::WriteOnly))request.write(args.contains("--stop-running")?"stop":"request");QTextStream(stdout)<<(quit?"Closing mixer controller.\n":"Opening the running Linux Mixer.\n");return 0;}
    QFile::remove(stateRoot()+"/.runtime/show.request");QFile::remove(stateRoot()+"/.runtime/quit.request");
    if(args.contains("--quit-running")||args.contains("--stop-running"))return 0;
    // One controller per desktop audio session, including installed/development copies.
    QLockFile sessionLock(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)+"/linux-mixer-controller.lock");
    const bool graphUtility=args.contains("--write-hardware")||args.contains("--link-hardware");
    if(!graphUtility&&!sessionLock.tryLock(100)){QTextStream(stderr)<<"Another Linux Mixer controller is using this audio session. Close that copy first.\n";return 1;}
    Engine engine;
    if(args.contains("--write-hardware")){
        const int i=args.indexOf("--write-hardware");if(i+1>=args.size())return 2;
        for(int j=0;j<5;++j){auto& hw=engine.settings.hardware(j);hw.mic=QString("linux_mixer.test.feed%1").arg(j);hw.bypass=true;hw.gain=0;hw.mute=false;}
        QFile f(args[i+1]);if(!f.open(QIODevice::WriteOnly))return 1;f.write(QJsonDocument(engine.graphConfiguration()).toJson());return 0;
    }
    if(args.contains("--link-hardware")){
        QProcess dump;dump.start("pw-dump");if(!dump.waitForFinished(5000)||dump.exitCode())return 1;
        QString prefix="linux_mixer";const int pi=args.indexOf("--prefix");if(pi>=0&&pi+1<args.size())prefix=args[pi+1];
        for(const auto& pair:hardwareLinks(QJsonDocument::fromJson(dump.readAllStandardOutput()).array(),engine.settings,prefix)){
            QProcess link;link.start("pw-link",{QString::number(pair.first),QString::number(pair.second)});if(!link.waitForFinished(2000))return 1;
        }return 0;
    }
    Console window(engine);Console& console=window;
    const bool testing=args.contains("--health-ui-test")||(args.contains("--position-test")||args.contains("--live-controls-test")||args.contains("--plugin-routing-test"))||args.contains("--smoke-test")||args.contains("--app-routing-test")||args.contains("--screenshot");
    QSystemTrayIcon tray;QMenu trayMenu;
    QPixmap icon(64,64);icon.fill(QColor("#20333d"));{QPainter p(&icon);p.setPen(QPen(QColor("#7aceaa"),5));for(int i=0;i<3;++i){p.drawLine(16+i*16,10,16+i*16,54);p.drawEllipse(QPoint(16+i*16,24+i*9),5,5);}}
    app.setWindowIcon(QIcon(icon));tray.setIcon(QIcon(icon));tray.setToolTip("Linux Mixer · audio and app routing running");
    auto showWindow=[&]{setDesktopMaximized(window,true);window.raise();window.activateWindow();};
    trayMenu.addAction("Show mixer",&window,showWindow);
    trayMenu.addAction("Restart audio engine",&window,[&]{engine.start();});
    trayMenu.addAction("Stop audio engine",&window,[&]{engine.stop();});
    auto* login=trayMenu.addAction("Start at login");login->setCheckable(true);
    const QString autostart=QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)+"/autostart/linux-mixer.desktop";
    login->setChecked(QFile::exists(autostart));
    QObject::connect(login,&QAction::triggered,&window,[&,autostart](bool enabled){
        if(!enabled){QFile::remove(autostart);return;}
        QDir().mkpath(QFileInfo(autostart).absolutePath());QString exe=QCoreApplication::applicationFilePath();exe.replace("\\","\\\\").replace("\"","\\\"").replace("`","\\`").replace("$","\\$").replace("%","%%");
        QFile file(autostart);if(file.open(QIODevice::WriteOnly))file.write(("[Desktop Entry]\nType=Application\nName=Linux Mixer\nExec=\""+exe+"\" --background\nTerminal=false\n").toUtf8());
        else {login->setChecked(false);engine.message="Could not save login startup entry";}
    });
    trayMenu.addSeparator();trayMenu.addAction("Quit and stop audio",&window,[&]{engine.stop();window.closeToBackground={};app.quit();});
    tray.setContextMenu(&trayMenu);QObject::connect(&tray,&QSystemTrayIcon::activated,&window,[&](QSystemTrayIcon::ActivationReason reason){if(reason==QSystemTrayIcon::Trigger||reason==QSystemTrayIcon::DoubleClick)showWindow();});
    QTimer reopen;reopen.setInterval(700);QObject::connect(&reopen,&QTimer::timeout,&window,[&]{const QString muteFile=stateRoot()+"/.runtime/toggle-mic.request";if(QFile::exists(muteFile)){QFile::remove(muteFile);engine.settings.mute=!engine.settings.mute;engine.apply();engine.save();if(engine.changed)engine.changed();}const QString quitFile=stateRoot()+"/.runtime/quit.request";QFile request(quitFile);if(request.open(QIODevice::ReadOnly)){const bool stop=request.readAll()=="stop";request.close();QFile::remove(quitFile);if(stop)engine.stop();window.closeToBackground={};app.quit();return;}const QString file=stateRoot()+"/.runtime/show.request";if(QFile::exists(file)){QFile::remove(file);showWindow();}});
    if(!testing){app.setQuitOnLastWindowClosed(false);window.closeToBackground=[&]{window.hide();};tray.show();reopen.start();}

    window.setDesktopMaximized=[&window](bool maximize){setDesktopMaximized(window,maximize);};
    if(args.contains("--window-size")){
        const int i=args.indexOf("--window-size");if(i+1>=args.size())return 2;
        const auto parts=args[i+1].split('x');if(parts.size()!=2)return 2;
        window.resize(parts[0].toInt(),parts[1].toInt());
    }
    if((args.contains("--position-test")||args.contains("--live-controls-test")||args.contains("--plugin-routing-test")) || args.contains("--window-size") || args.contains("--smoke-test") || args.contains("--app-routing-test"))window.show();
    else if(!args.contains("--background"))setDesktopMaximized(window,true);
    if(args.contains("--health-ui-test"))runHealthUiTest(app,console);
    if(args.contains("--plugin-routing-test"))runPluginRoutingTest(app,engine,console);
    if(args.contains("--position-test"))runPositionTest(app,engine,console);
    if(args.contains("--live-controls-test"))runLiveControlsTest(app,engine,console);
    if(args.contains("--smoke-test"))runSmoke(app,engine,console);
    if(args.contains("--app-routing-test"))runAppRoutingTest(app,engine);
    if(args.contains("--screenshot")){
        const int i=args.indexOf("--screenshot");
        if(i+1>=args.size())return 2;
        const QString path=args[i+1];
        QTimer::singleShot(2600,&app,[&,path]{window.grab().save(path);app.quit();});
    }
    if(args.contains("--start-engine"))QTimer::singleShot(600,&app,[&]{engine.start();});
    return app.exec();
}
