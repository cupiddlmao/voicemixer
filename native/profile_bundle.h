#pragma once
#include <QtCore/QJsonObject>
#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <array>

struct ProfileBundle {
    QJsonObject settings;
    std::array<QJsonObject,8> racks;
    bool legacy=false;
};
constexpr qint64 profileSizeLimit=64*1024*1024;
bool parseProfile(const QByteArray&,ProfileBundle&,QString& error);
QByteArray encodeProfile(const ProfileBundle&);
bool readProfile(const QString& path,ProfileBundle&,QString& error);
bool readProfileRacks(const QString& root,ProfileBundle&,QString& error);
bool writeProfileFile(const QString& path,const QByteArray&,QString& error);
// Call only after stopping plugin workers. Keeps a rollback backup and rolls
// back completed writes if any later write fails. Legacy presets keep racks.
bool installProfile(const QString& root,const ProfileBundle&,QString& backup,QString& error);
struct ProfilePlugin {QString name,format,file;};
bool profilePlugin(const QString& xml,ProfilePlugin&,QString& error);
QString relocateProfilePlugin(const QString& xml,const QString& file);
