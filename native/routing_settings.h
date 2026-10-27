#pragma once
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <array>

struct RecorderSettings;
struct RoutingSettings {
    std::array<QString,5> outputs{};
    std::array<double,3> inputGain{};
    std::array<double,5> hardwarePan{};
    // Front/back cue: -1 front (up to +3 dB dry), 0 neutral, +1 back (-9 dB dry; reverb/delay sends stay, so it sounds further away).
    std::array<double,5> hardwareDepth{};
    std::array<bool,3> inputMute{};
    std::array<double,8> busGain{};
    std::array<bool,8> busMute{};
    // Rows: hardware 1..5, System, Music, AUX. Columns: A1..A5, B1..B3.
    std::array<std::array<bool,8>,8> routes{};
    std::array<std::array<double,6>,3> eq{};
    std::array<bool,8> busComposite{},busCompositePost{},busMono{};
    std::array<std::array<int,8>,8> compositePatch{};
    std::array<double,8> busBalance{};
    std::array<double,8> reverbSend{},delaySend{},fxAmount{},reverbReturn{},delayReturn{};
    bool reverbEnabled=true,delayEnabled=true;
    double room=.55,damping=.35,delayMs=350,feedback=.3;
    QJsonObject appRules;
    RoutingSettings();
    double panGain(int input,int channel) const;
    double depthGain(int input) const;
    double dryGain(int input,int channel) const;
    QJsonObject json() const;
    void load(const QJsonObject&);
};
QString virtualInputName(int index,const QString& prefix="linux_mixer");
QString virtualInputDescription(int index);
QString busCaptureName(int index,const QString& prefix="linux_mixer");
QString busOutputName(int index,const QString& prefix="linux_mixer");
QJsonObject matrixConfiguration(const RoutingSettings&,const QString& prefix="linux_mixer",const RecorderSettings* tape=nullptr);
