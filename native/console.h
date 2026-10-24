#pragma once
#include <QtWidgets/QWidget>
#include <QtCore/QRectF>
#include <vector>
#include "engine.h"

class QPainter;
class QTimer;
class QDialog;
void styleMixerDialog(QDialog&);
class Console : public QWidget {
public:
    explicit Console(Engine& engine);
    ~Console() override;
    void saveScreenshot(const QString& path);
    std::function<void(bool)> setDesktopMaximized;
    std::function<void()> closeToBackground;
protected:
    void closeEvent(QCloseEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
private:
    Engine& e;
    enum Action { None, Mic, Output, Start, Menu, Comp, Gate, Denoise, Bypass,
                  Color, Gain, MasterGain, MasterMute, VirtualGain, VirtualMute, Route, Apps, Mute, Timing, ResetColor, Eq, TapeLoad, TapeSeek, TapeTransport, TapeRoute, TapeOptions, Plugins, BusMode, FxSend, FxOptions, Balance, BusMono };
    struct Hit {QRectF rect;Action action;QString tip;int first=-1,second=-1;};
    std::vector<Hit> hits;
    Action dragging=None;
    QPointF dragStart;
    double dragValue=0;
    int dragIndex=0,dragBand=0,dragAxis=0;
    QTimer* transportTimer=nullptr;
    int transportDirection=0;
    QPointF logical(const QPointF&) const;
    const Hit* hit(const QPointF&) const;
    void add(QRectF,Action,const QString&,int first=-1,int second=-1);
    void activate(Action,const QPointF&,int first=-1,int second=-1);
    void drag(const QPointF&);
    void chooseDevice(bool input,int bus=0);
    void applications(int input=-1);
    void options();
    void recorderOptions();
    void busOptions(int);
    void fxOptions(int);
    void timing(int index=0);
    void strip(QPainter&,int index,bool hardware);
    void masters(QPainter&);
};
