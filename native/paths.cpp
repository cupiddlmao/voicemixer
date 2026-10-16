#include "paths.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QStandardPaths>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
QString stateRoot(){
    const auto override=qEnvironmentVariable("LINUX_MIXER_STATE_DIR");if(!override.isEmpty())return QDir(override).absolutePath();
    const auto development=QCoreApplication::applicationDirPath()+"/../native/main.cpp";
    if(QFileInfo::exists(development))return QDir(QCoreApplication::applicationDirPath()+"/..").absolutePath();
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}
QString pluginPath(const QString& name){
    const QString app=QCoreApplication::applicationDirPath();
    for(const auto& dir:{app+"/plugins",app+"/../lib/linux-mixer",app,app+"/../.deps"})if(QFileInfo::exists(dir+"/"+name))return QDir::cleanPath(dir+"/"+name);
    return {};
}
