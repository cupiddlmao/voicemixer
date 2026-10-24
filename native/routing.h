#pragma once
#include "routing_settings.h"
#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QJsonArray>
#include <QtCore/QSet>
#include <QtCore/QQueue>
#include <array>
#include <memory>
class Engine;
struct PlaybackApp { QString key,name;QList<int> streams;int input=-1; };

class RoutingMatrix : public QObject {
public:
    explicit RoutingMatrix(Engine&);
    ~RoutingMatrix() override;
    bool ready=false;
    std::array<double,3> inputPeak{{-90,-90,-90}};
    std::array<double,8> busPeak{{-90,-90,-90,-90,-90,-90,-90,-90}};
    QList<PlaybackApp> apps;
    void consume(const QJsonArray&,bool microphoneCompatible);
    void apply();
    void stop();
    void shutdown();
    void setRoute(int input,int bus,bool enabled);
    void setOutput(int bus,const QString& device);
    void assignApp(const QString& key,int input);
    void refreshApps();
    void rulesChanged();
    QString nodeForInput(int input)const;
private:
    struct Port {int id,node;QString channel,name,direction;};
    struct Command {QString program;QStringList args;bool quiet=false;};
    Engine& engine;
    QString runtime;
    QProcess worker,appQuery;
    QQueue<Command> commands;
    QSet<QString> queued,links;
    QHash<QString,int> nodeIds;
    QHash<QString,qint64> nodeSerials;
    QHash<QString,QPair<qint64,QJsonArray>> lastParameters;
    QHash<int,QString> sinkBySerial;
    QSet<int> hardwareSinkIds;
    QList<Port> ports;
    std::array<std::unique_ptr<QProcess>,11> meters;
    std::array<QByteArray,11> buffers;
    QHash<int,QString> streamTokens;
    QSet<QString> appliedStreams;
    qint64 retryAfter=0;
    bool starting=false,failed=false,stopping=false,dirty=false;
    void start();
    void queue(const QString&,const QStringList&,bool quiet=false);
    void next();
    void synchronizeLinks();
    void link(int output,int input);
    int port(const QString& node,const QString& channel,const QString& direction)const;
    void startMeters();
    void parseApps(const QJsonArray&);
    qint64 ownedPid()const;
};
