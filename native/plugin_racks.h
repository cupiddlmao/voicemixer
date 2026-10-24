#pragma once
#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <array>
class Engine;
class PluginRacks : public QObject {
public:
    explicit PluginRacks(Engine&);
    ~PluginRacks() override;
    void sync(bool active);
    void open(int input);
    void shutdown();
    bool running(int input) const;
    qint64 processId(int input) const{return hosts.at(input).processId();}
    bool hasFailed(int input) const{return failed.at(input);}
private:
    Engine& engine;
    std::array<QProcess,8> hosts;
    std::array<bool,8> failed{};
    bool active=false,closing=false;
    QString folder(int input) const;
    QString executable() const;
    void launch(int input,bool background);
};
QString pluginNodeName(int input);
