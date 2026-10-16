#pragma once
#include <QtCore/QJsonArray>
#include "engine.h"
// Return desired physical-source -> mono capture-port links from a registry snapshot.
QList<QPair<int,int>> hardwareLinks(const QJsonArray&,const Settings&,const QString& prefix="linux_mixer");
