#pragma once
#include "profile_bundle.h"
#include "engine.h"
class QWidget;
// Returns false on Cancel, without modifying mixer state or profile files.
bool reviewProfile(ProfileBundle&,const Settings& current,const QList<Device>&,QWidget* parent=nullptr);
