#pragma once
#include <QtCore/QObject>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QTimer>
#include <QtCore/QDateTime>
#include <QtCore/QStringList>
#include <functional>
#include <memory>
#include "routing_settings.h"
#include "recorder.h"
class RoutingMatrix;
class PluginRacks;
class AudioHealth;

struct Device { QString name, description; bool input; QStringList channels; };
struct HardwareSettings {
    QString mic, channel="MONO";
    double threshold=-42, attack=5, release=180, compression=0;
    double colorX=.5, colorY=.5, gain=0;
    bool denoise=true, bypass=false, mute=false, positionPanel=false;
    QJsonObject json() const;
    void load(const QJsonObject&);
};
QString captureName(int index);
QString processedName(int index);
struct Settings : HardwareSettings {
    QString sink;
    bool engineEnabled=false;
    std::array<HardwareSettings,4> extra;
    HardwareSettings& hardware(int i){return i==0?static_cast<HardwareSettings&>(*this):extra.at(i-1);}
    const HardwareSettings& hardware(int i)const{return i==0?static_cast<const HardwareSettings&>(*this):extra.at(i-1);}
    double busGain=0;
    double auxGain=0;
    bool broadcast=true, busMute=false;
    bool auxMute=false, auxA1=true;
    std::array<bool,8> pluginRacks{};
    RoutingSettings routing;
    RecorderSettings tape;
    QJsonObject json() const;
    void load(const QJsonObject&);
};

class Engine : public QObject {
public:
    explicit Engine(QObject* parent=nullptr);
    ~Engine() override;
    Settings settings;
    std::unique_ptr<RoutingMatrix> matrix;
    std::unique_ptr<Recorder> recorder;
    std::unique_ptr<PluginRacks> plugins;
    std::unique_ptr<AudioHealth> health;
    QList<Device> devices;
    QString message="Connecting to PipeWire…";
    bool running=false, monitoring=false, connected=false;
    double peakDb=-90, heldDb=-90;
    double auxPeakDb=-90;
    std::array<double,5> hardwarePeak{{-90,-90,-90,-90,-90}};
    bool auxRunning=false;
    std::function<void()> changed;
    void refresh();
    void start();
    void stop();
    void apply();
    void save();
    void setMonitor(bool);
    void ensureAux(bool restart=false);
    void exportPreset(const QString&);
    void importPreset(const QString&);
    void shutdown();
    QJsonObject graphConfiguration()const{return configuration();}
private:
    QTimer refreshTimer, applyTimer, saveTimer;
    QProcess discovery, control, busControl, meter, auxControl, auxMeter;
    QByteArray meterBuffer;
    QByteArray auxMeterBuffer;
    int captureId=-1, sourceId=-1, broadcastId=-1, auxId=-1;
    bool profileOperation=false;
    bool starting=false, pending=false, stoppedExplicitly=false, restartRequested=false;
    qint64 retryAfter=0;
    int recoveryAttempts=0;
    bool auxStarting=false, auxFailed=false;
    QString root, runtime;
    void notify();
    void sendControls();
    void startMeter();
    void stopOwned(const QString&);
    qint64 ownedPid(const QString&) const;
    std::array<int,4> extraCapture{{-1,-1,-1,-1}};
    std::array<std::unique_ptr<QProcess>,4> extraControls, extraMeters;
    QJsonObject configuration() const;
    bool writeJson(const QString&, const QJsonObject&);
};
