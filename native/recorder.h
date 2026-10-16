#pragma once
#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QTimer>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QQueue>
#include <array>
#include <functional>
struct RecorderSettings {
    QString directory,prefix="Record",format="WAV";
    bool busMode=false,multitrack=false,playOnLoad=false,loop=false;
    int bus=0,channels=2,bits=16,rate=48000,kbps=192,bufferSeconds=20;
    double preSeconds=0,stopAfter=0,gain=0;
    std::array<bool,8> armed{{true,true,true,true,true,true,true,true}};
    std::array<bool,8> playback{{true,false,false,false,false,false,false,false}};
    QJsonObject json()const;void load(const QJsonObject&);
};
struct RecorderSource {QString node,channel="MONO";};
QString mediaTool(const QString& name);
class Recorder:public QObject {
public:
    Recorder(RecorderSettings&,std::function<std::array<RecorderSource,5>()>,QObject* parent=nullptr,QString prefix="linux_mixer");
    ~Recorder()override;
    QString file,recordFile,error;
    bool recording=false,recordPaused=false,finalizing=false,playing=false,playPaused=false,loading=false,inputReady=false;
    double position=0,duration=0,peak=-90;
    std::function<void()> changed,needGraph;
    void consume(const QJsonArray&);
    void configure();
    void setActive(bool);
    void loadFile(const QString&,bool honorAutoplay=true);
    void play();void pause();void stop();void record();void seek(double);void eject();void shutdown();
    int completedPlays=0;
    int captureChannels()const{return captureCount;}
private:
    RecorderSettings& s;std::function<std::array<RecorderSource,5>()> physical;
    QString prefix;
    QProcess capture,encoder,decoder,player,probe,linker;
    QQueue<QPair<int,int>> linkQueue;
    QByteArray captureBytes,history,pendingAudio;
    QElapsedTimer playClock;
    QTimer clockTimer;
    qint64 recordedFrames=0,playedFrames=0;
    int captureCount=16,outputChannels=2;
    bool configuredBus=false,decoderPending=false,playLinksReady=false,closing=false,failedRecord=false,changingCapture=false,active=true;
    double playStart=0;
    void notify();void startCapture();void receive();void feed(const QByteArray&);
    void flushRecording();
    void startDecoder();void stopPlayback();void nextLink();void finishRecording(int);
};
