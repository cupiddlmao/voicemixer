#pragma once
#include <QtCore/QObject>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtCore/QSettings>
#include <functional>
class QWidget;
struct ReleaseUpdate {QString version;QUrl url;bool preview=false;};
ReleaseUpdate newestUpdate(const QByteArray& json,const QString& current,bool previews,QString* error=nullptr);
class UpdateChecker : public QObject {
public:
    explicit UpdateChecker(QObject* parent=nullptr,QNetworkAccessManager* transport=nullptr);
    void showDialog(QWidget* parent);
    void check();
    QString status="Update checks are manual unless you enable checking at startup.";
    ReleaseUpdate update;
    bool busy=false;
    std::function<void()> changed;
private:
    QNetworkAccessManager* network;QSettings preferences;
    QNetworkReply* reply=nullptr;QByteArray response;
};
