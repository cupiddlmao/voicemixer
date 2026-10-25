#pragma once
#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QTimer>
#include <QtCore/QHash>
#include <QtCore/QStringList>
#include <QtCore/QElapsedTimer>
#include <functional>
struct AudioNodeHealth {
    int id=0,quantum=0,rate=0;quint64 errors=0,sessionErrors=0;
    double load=-1;QString name,state;bool driver=false;
};
QList<AudioNodeHealth> parseAudioHealth(const QByteArray&);
quint64 audioErrorDelta(const AudioNodeHealth&,QHash<QString,quint64>& previous);
class AudioHealthStream {
    QByteArray partial,snapshot;
public:
    QList<QByteArray> append(const QByteArray&);
    void clear(){partial.clear();snapshot.clear();}
};
class AudioHealth : public QObject {
public:
    explicit AudioHealth(QObject* parent=nullptr,QString tool="pw-top");
    ~AudioHealth() override;
    QList<AudioNodeHealth> nodes;
    QString status="Waiting for audio statistics";
    bool available=false;
    quint64 errors=0;
    double load=-1,periodMs=0;
    int quantum=0,rate=0;
    std::function<void()> changed;
    void setRelevantNodes(const QStringList& names){relevant=names;}
    void reset();
    void start();
    void stop();
private:
    QProcess query;QTimer poll;
    QStringList relevant;QString executable;
    QHash<QString,quint64> previous,totals;
    AudioHealthStream stream;
    QElapsedTimer monitorAge,lastSnapshot;
    bool running=false;
    void sample();
    void consume(const QByteArray&);
};
