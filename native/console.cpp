#include "audio_health.h"
#include "update_checker.h"
#include "console.h"
#include <QtGui/QCloseEvent>
#include <QtWidgets/QApplication>
#include "routing.h"
#include "plugin_racks.h"
#include <QtGui/QPainter>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtGui/QContextMenuEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>
#include <QtWidgets/QToolTip>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtCore/QFileInfo>
#include <QtCore/QTimer>
#include <algorithm>
#include <cmath>

namespace {
constexpr double W=1480,H=720;
const QColor bg("#344a57"), panel("#2d414c"), ink("#b4c7d0"), dim("#9bb2be"), amber("#e8c47a"),
    line("#5d7784"), green("#8be1bd"), dark("#1d303a"), coral("#f58069");
void text(QPainter& p,QRectF r,const QString& s,int size=10,QColor c=ink,int align=Qt::AlignLeft|Qt::AlignVCenter,bool bold=false) {
    // Resize the layout area, while keeping glyphs proportional. Anchor each
    // label at its mapped origin rather than stretching the font with the panel.
    p.save();const auto t=p.worldTransform();
    const double sx=std::hypot(t.m11(),t.m12()),sy=std::hypot(t.m21(),t.m22());
    const double scale=std::min(sx,sy);const QPointF origin=t.map(r.topLeft());
    p.setWorldTransform(QTransform(t.m11()/sx*scale,t.m12()/sx*scale,
        t.m21()/sy*scale,t.m22()/sy*scale,origin.x(),origin.y()));
    QFont f("DejaVu Sans",size);f.setBold(bold);p.setFont(f);p.setPen(c);
    p.drawText(QRectF(0,0,r.width()*sx/scale,r.height()*sy/scale),align,s);p.restore();
}
void box(QPainter& p,QRectF r,QColor fill=panel,QColor border=line,double radius=3) {
    p.setPen(QPen(border,.8));p.setBrush(fill);p.drawRoundedRect(r,radius,radius);
}
void button(QPainter& p,QRectF r,const QString& s,bool active=false,bool enabled=true,int size=9) {
    box(p,r,active?QColor("#466b67"):bg,enabled?(active?green:line):QColor("#48606d"),4);
    text(p,r,s,size,enabled?(active?green:ink):QColor("#59717e"),Qt::AlignCenter);
}
void knob(QPainter& p,QPointF c,double radius,double amount,const QString& value,bool enabled=true,bool bipolar=false) {
    const auto t=p.worldTransform();const double scale=std::min(t.m11(),t.m22());
    const double rx=radius*scale/t.m11(),ry=radius*scale/t.m22();
    p.setPen(QPen(enabled?line:QColor("#48606b"),1.1));p.setBrush(bg);p.drawEllipse(c,rx,ry);
    if(enabled) {
        p.setPen(QPen(green,1.8));p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(c.x()-rx,c.y()-ry,2*rx,2*ry),bipolar?90*16:225*16,-int(270*16*(bipolar?amount-.5:amount)));
        const double a=(135+270*amount)*3.141592653589793/180;
        p.setPen(QPen(green,1.8));p.drawLine(c+QPointF(std::cos(a)*rx*.72,std::sin(a)*ry*.72),c+QPointF(std::cos(a)*rx*.9,std::sin(a)*ry*.9));
    }
    text(p,QRectF(c.x()-radius,c.y()-9,2*radius,18),value,8,enabled?ink:QColor("#5c7480"),Qt::AlignCenter);
}
void meter(QPainter& p,QRectF r,double db,bool live) {
    box(p,r,QColor("#13232c"),QColor("#546b77"),1);
    const int count=int(r.height()/5);
    const double level=std::clamp((db+60)/60.,0.,1.);
    for(int i=0;i<count;++i) {
        const bool lit=live && double(i)/count<level;
        QColor c=lit ? (i>count*.93?coral:(i>count*.78?QColor("#c4d488"):green)) : QColor("#263e47");
        p.fillRect(QRectF(r.x()+3,r.bottom()-4-i*5,r.width()-6,3),c);
    }
}
void fader(QPainter& p,QRectF r,double value,bool live,const QString& label="Fader Gain",bool drawScale=true,QColor accent=green) {
    const double t=std::clamp((12-value)/72.,0.,1.);
    const double yy=r.y()+18+t*(r.height()-36);
    box(p,QRectF(r.center().x()-7,r.y(),14,r.height()),dark,live?accent:line,6);
    if(live){p.setPen(Qt::NoPen);p.setBrush(accent==green?QColor("#7aceaa"):accent.darker(110));p.drawRoundedRect(QRectF(r.center().x()-6,yy,12,r.bottom()-yy),5,5);}
    if(drawScale)for(int db:{12,0,-12,-24,-36,-48,-60}) {
        const double y=r.y()+18+(12-db)/72.*(r.height()-36);
        p.setPen(QPen(dim,.6));p.drawLine(QPointF(r.x()-3,y),QPointF(r.x()-1,y));
        text(p,QRectF(r.x()-24,y-6,19,12),QString::number(db),6,dim,Qt::AlignRight|Qt::AlignVCenter);
    }
    p.save();p.translate(r.center().x(),r.bottom()-13);p.rotate(-90);text(p,QRectF(0,-7,r.height()-50,14),label,8,live?dark:dim);p.restore();
    const auto transform=p.worldTransform();const double scale=std::min(transform.m11(),transform.m22());
    p.setPen(QPen(live?accent:line,1.2));p.setBrush(live?(accent==green?QColor("#5e8790"):QColor("#7a6a45")):bg);p.drawEllipse(QPointF(r.center().x(),yy),17*scale/transform.m11(),17*scale/transform.m22());
    text(p,QRectF(r.center().x()-18,yy-8,36,16),value<=-60?"−∞":QString::number(value,'f',value==std::floor(value)?0:1),7,live?QColor("#e0f6ed"):dim,Qt::AlignCenter,true);
}
}
void styleMixerDialog(QDialog& dialog){
    QPalette colors=dialog.palette();
    colors.setColor(QPalette::Window,bg);colors.setColor(QPalette::WindowText,ink);
    colors.setColor(QPalette::Base,dark);colors.setColor(QPalette::AlternateBase,panel);
    colors.setColor(QPalette::Text,ink);colors.setColor(QPalette::Button,panel);colors.setColor(QPalette::ButtonText,ink);
    colors.setColor(QPalette::Highlight,QColor("#466b67"));colors.setColor(QPalette::HighlightedText,Qt::white);
    colors.setColor(QPalette::ToolTipBase,dark);colors.setColor(QPalette::ToolTipText,ink);
    colors.setColor(QPalette::Disabled,QPalette::Text,dim);colors.setColor(QPalette::Disabled,QPalette::WindowText,dim);colors.setColor(QPalette::Disabled,QPalette::ButtonText,dim);
    dialog.setPalette(colors);
}
Console::Console(Engine& engine):e(engine) {
    setWindowTitle("Linux Mixer · Potato console");resize(1480,720);setMinimumSize(900,430);setMouseTracking(true);
    updates=std::make_unique<UpdateChecker>(this);updates->changed=[this]{update();};
    auto* frameTimer=new QTimer(this);frameTimer->setSingleShot(true);frameTimer->setInterval(16);connect(frameTimer,&QTimer::timeout,this,[this]{update();});
    e.changed=[frameTimer]{if(!frameTimer->isActive())frameTimer->start();};transportTimer=new QTimer(this);transportTimer->setInterval(180);connect(transportTimer,&QTimer::timeout,this,[this]{e.recorder->seek(e.recorder->position+transportDirection*2);});
}
Console::~Console(){e.changed={};}
void Console::closeEvent(QCloseEvent* event){if(closeToBackground){event->ignore();closeToBackground();}else QWidget::closeEvent(event);}
void Console::saveScreenshot(const QString& path){grab().save(path);}
QPointF Console::logical(const QPointF& p)const{return {p.x()*W/width(),p.y()*H/height()};}
const Console::Hit* Console::hit(const QPointF& p)const{for(auto it=hits.rbegin();it!=hits.rend();++it)if(it->rect.contains(p))return &*it;return nullptr;}
void Console::add(QRectF r,Action a,const QString& tip,int first,int second){hits.push_back({r,a,tip,first,second});}
void Console::paintEvent(QPaintEvent*) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),bg);
    p.scale(width()/W,height()/H);p.fillRect(QRectF(0,0,W,H),bg);hits.clear();
    p.fillRect(QRectF(5,6,47,23),QColor("#e76b54"));text(p,{5,6,47,23},"VOICE",10,Qt::white,Qt::AlignCenter,true);
    text(p,{56,6,64,23},"MIXER",10,ink,Qt::AlignLeft|Qt::AlignVCenter,true);
    text(p,{126,5,110,26},"Linux",16,QColor("#efd6a0"));
    text(p,{246,5,300,26},"PERSONAL AUDIO MIXING CONSOLE",8,dim);
    button(p,{895,7,186,23},"Applications",false,true,9);add({895,7,186,23},Apps,"Assign running apps to System, Music or AUX; choices are remembered");
    button(p,{1100,7,90,23},e.running?"ENGINE ON":"START",e.running,true,8);add({1100,7,90,23},Start,"Start or restart the audio engine with the selected microphone");
    {const bool known=e.health->available&&e.health->rate>0&&e.health->periodMs>0;
     button(p,{1199,7,134,23},known?QString("%1 Hz  ·  %2 ms").arg(e.health->rate).arg(e.health->periodMs,0,'f',1):"48 000 Hz  ·  STEREO",false,known,8);
     if(known)add({1199,7,134,23},Health,"Driver sample rate and scheduling period (not total latency). Click for audio health");}
    {bool anyMic=false;for(int i=0;i<5;++i)anyMic|=!e.settings.hardware(i).mic.isEmpty();
     if(!e.running){const QString guide=!anyMic?"1 Pick a mic  ·  2 Pick headphones  ·  3 START":e.settings.routing.outputs[0].isEmpty()?"2 Pick headphones (A1)  ·  3 START":"3 Press START";
        text(p,{545,5,345,26},guide,8,amber,Qt::AlignCenter);}}
    button(p,{1345,7,122,23},updates->update.version.isEmpty()?"Menu":"Menu · Update",false,true,9);add({1345,7,122,23},Menu,"Presets, gate timing, stop engine and quit");
    p.setPen(QPen(line,.8));p.drawLine(QPointF(7,38),QPointF(1471,38));
    for(int i=0;i<8;++i)strip(p,i,i<5);
    masters(p);
    p.fillRect(QRectF(7,688,1465,24),dark);
    p.setBrush(e.running?green:coral);p.setPen(Qt::NoPen);p.drawEllipse(QPointF(19,700),3,3);
    text(p,{30,688,1100,24},QFontMetrics(QFont("DejaVu Sans",9)).elidedText(e.message,Qt::ElideRight,1090),9,e.running?ink:QColor("#e0bc9e"));
    QString healthText="Audio health…";if(e.health->available)healthText=QString("Audio health · %1 errors").arg(e.health->errors);
    text(p,{1155,688,304,24},healthText,8,e.health->errors?coral:dim,Qt::AlignRight|Qt::AlignVCenter);add({1155,688,304,24},Health,"Audio buffer timing, processing load, node errors and plugin recovery");
}
void Console::strip(QPainter& p,int index,bool hardware) {
    const double x=8+index*129;const bool live=hardware,virtualInput=!hardware,usable=true;const int vi=index-5;const auto& s=e.settings.hardware(hardware?index:0);const auto& routing=e.settings.routing;
    p.setPen(QPen(line,.6));p.drawLine(QPointF(x+124,45),QPointF(x+124,678));
    text(p,{x+3,45,120,20},hardware?QString("HARDWARE INPUT %1").arg(index+1):QString("VIRTUAL INPUT %1").arg(index-4),8,live?ink:dim);
    QString name="Select Input Device";
    if(live) {name=s.mic.isEmpty()?"Select Microphone":s.mic;for(const auto& d:e.devices)if(d.name==s.mic)name=d.description;}
    else if(virtualInput)name=virtualInputDescription(vi);
    box(p,{x+2,68,120,27},usable?QColor("#3d5664"):bg,line,2);
    text(p,{x+7,68,109,27},QFontMetrics(QFont("DejaVu Sans",8)).elidedText(name,Qt::ElideRight,108),8,usable?green:dim);
    if(live)add({x+2,68,120,27},Mic,"Select physical microphone or interface channel. Changing devices restarts the engine.",index);
    if(virtualInput)add({x+2,68,120,27},Apps,"Assign running apps to this input",vi);
    if(hardware) {
        text(p,{x+6,103,112,16},"INTELLIPAN",8,dim,Qt::AlignCenter);
        box(p,{x+7,125,110,114},dark,QColor("#20333d"),2);
        p.setPen(QPen(QColor("#344c58"),.8));p.drawLine(QPointF(x+62,147),QPointF(x+62,225));p.drawLine(QPointF(x+16,186),QPointF(x+109,186));
        text(p,{x+10,127,102,17},s.positionPanel?"POSITION   L/R · F/B":"VOICE      Color Panel",7,live?ink:dim);
        text(p,{x+9,150,106,14},s.positionPanel?"Front":"Presence",7,dim,Qt::AlignCenter);
        text(p,{x+11,219,103,14},s.positionPanel?"Left     Center     Right":"Warm              Bright",7,dim);
        const QPointF pos(x+16+(s.positionPanel?(routing.hardwarePan[index]+1)*.5:s.colorX)*92,s.positionPanel?186+std::clamp(routing.hardwareDepth[index],-1.,1.)*39:165+s.colorY*48);
        if(s.positionPanel)text(p,{x+9,205,106,14},"Back",7,dim,Qt::AlignCenter);
        p.setPen(Qt::NoPen);p.setBrush(live?coral:QColor("#647e8b"));p.drawRoundedRect(QRectF(pos.x()-5,pos.y()-5,10,10),2,2);
        if(live)add({x+7,125,110,114},Color,s.positionPanel?"Drag to position this input: left/right pans, up/down moves it front (louder, closer) or back (quieter, more reverb). Double-click to center. Right-click for Color Panel.":"Drag to shape warmth, brightness and presence. Double-click to reset. Right-click for left/right position.",index);
    } else {
        text(p,{x+5,104,115,18},"EQUALIZER",8,dim,Qt::AlignCenter);
        for(int k=0;k<6;++k){const QPointF center(x+28+(k%3)*33,k<3?155:207);const double value=routing.eq[vi][k];knob(p,center,15,(value+12)/24,QString::number(value,'f',1),true);add({center.x()-16,center.y()-16,32,32},Eq,QString("%1 channel %2 EQ gain (dB); double-click for flat").arg(k<3?"Left":"Right",QStringList{"Treble","Mid","Bass"}[k%3]),vi,k);}
        text(p,{x+8,179,110,15},"Treble     Mid      Bass",7,dim,Qt::AlignCenter);text(p,{x+5,148,10,14},"L",7,ink);text(p,{x+5,200,10,14},"R",7,ink);
    }
    if(virtualInput){
        text(p,{x+8,247,109,17},"APPLICATIONS",7,dim);
        int row=0;
        for(const auto& app:e.matrix->apps)if(app.input==vi){
            if(row==4)break;
            const QRectF r(x+7,270+row*22,111,20);button(p,r,QFontMetrics(QFont("DejaVu Sans",7)).elidedText(app.name,Qt::ElideRight,102),false,true,7);add(r,Apps,"Change app assignments",vi);++row;
        }
        if(row==0)text(p,{x+9,271,105,36},"Click input name\nto assign apps",7,dim,Qt::AlignCenter);
    }
    if(hardware) {
        text(p,{x+8,247,52,17},"COMP",7,live?ink:dim,Qt::AlignCenter);text(p,{x+66,247,52,17},"GATE",7,live?ink:dim,Qt::AlignCenter);
        knob(p,{x+32,286},21,live?s.compression/10:0,live?QString::number(s.compression,'f',1):"0.0",live);
        knob(p,{x+91,286},21,live?(s.threshold+60)/60:0,live?QString::number(s.threshold,'f',0):"0.0",live);
        if(live){add({x+10,264,44,44},Comp,"Compressor amount. Drag vertically or use mouse wheel.",index);add({x+69,264,44,44},Gate,"Gate threshold in dB. Right-click for attack/release timing.",index);}
        text(p,{x+9,320,49,16},"DENOISE",7,live?ink:dim,Qt::AlignCenter);text(p,{x+64,320,49,16},"PROCESS",7,live?ink:dim,Qt::AlignCenter);
        button(p,{x+9,340,49,29},"RNNoise",live && s.denoise,live,7);
        button(p,{x+64,340,49,29},"Bypass",live && s.bypass,live,7);
        if(live){add({x+9,340,49,29},Denoise,"Toggle RNNoise noise suppression live",index);add({x+64,340,49,29},Bypass,"Bypass denoise, gate, compressor and color. Fader and mute stay active.",index);}
    }
    button(p,{x+9,376,54,21},e.plugins->hasFailed(index)?"FX error":"Plugins",e.plugins->running(index),true,7);add({x+9,376,54,21},Plugins,"Show or hide this input’s VST3/LADSPA plugin rack without stopping effects; right-click to bypass the rack (Windows VST3 via yabridge)",index);
    text(p,{x+66,376,48,21},"FX sends",7,dim,Qt::AlignCenter);
    for(int k=0;k<3;++k){const double value=k==0?routing.reverbSend[index]:k==1?routing.fxAmount[index]:routing.delaySend[index];knob(p,{x+25+k*35,415},13,value,QString::number(value*100,'f',0),true);add({x+11+k*35,401,28,28},FxSend,k==0?"Reverb send (%)":k==1?"FX send level (%)":"Delay send (%)",index,k);}
    text(p,{x+3,432,120,13},"Reverb    FX lvl    Delay",7,dim,Qt::AlignCenter);
    meter(p,{x+4,465,15,175},live?e.hardwarePeak[index]:virtualInput?e.matrix->inputPeak[vi]:-90,live?e.running:virtualInput && e.matrix->ready);
    fader(p,{x+46,465,25,175},live?s.gain:virtualInput?routing.inputGain[vi]:0,usable);
    if(live)add({x+29,457,48,191},Gain,"Microphone gain in dB. Double-click for 0 dB.",index);
    if(virtualInput)add({x+29,457,48,191},VirtualGain,"App playback level. Double-click for 0 dB.",vi);
    for(int b=0;b<8;++b) {
        const bool implemented=usable;
        const int inputRow=index;
        const bool active=usable && routing.routes[inputRow][b];
        const QString label=b<5?QString("▷A%1").arg(b+1):QString("▷B%1").arg(b-4);
        button(p,{x+83,465.+b*22,35,18},label,active,implemented,8);
        if(implemented)add({x+83,465.+b*22,35,18},Route,b<5?QString("Send this strip to hardware output A%1").arg(b+1):QString("Send this strip to virtual microphone B%1").arg(b-4),inputRow,b);
    }
    {const bool stereoPath=virtualInput||e.plugins->running(index);box(p,{x+6,651,52,23},dark,stereoPath?green:line,4);text(p,{x+6,651,52,23},virtualInput?"Stereo":e.plugins->running(index)?"Stereo FX":"Mono",8,stereoPath?green:ink,Qt::AlignCenter);}
    button(p,{x+64,651,54,23},"Mute",live?s.mute:virtualInput && routing.inputMute[vi],usable,8);
    if(live)add({x+64,651,54,23},Mute,"Mute this microphone output. From a terminal: linux-mixer --ctl mute mic<N> toggle",index);
    if(virtualInput){add({x+64,651,54,23},VirtualMute,"Mute this virtual input. From a terminal: linux-mixer --ctl mute system|music|aux toggle",vi);text(p,{x+7,448,110,12},e.matrix->ready?"APP PLAYBACK":"START ENGINE",7,dim,Qt::AlignCenter);}
    else if(s.mic.isEmpty())text(p,{x+7,448,110,12},"NOT CONFIGURED",7,QColor("#93a9b3"),Qt::AlignCenter);
    else text(p,{x+7,448,110,12},e.hardwarePeak[index]>=-.1?"CLIP":QString::number(e.hardwarePeak[index],'f',1)+" dBFS",7,e.hardwarePeak[index]>=-.1?coral:dim,Qt::AlignCenter);
}
void Console::masters(QPainter& p) {
    const double x=1048;
    text(p,{x+2,45,400,18},"HARDWARE OUT",9,ink);
    QString output=e.settings.routing.outputs[0].isEmpty()?"A1  ·  Select headphone output":"A1  ·  "+e.settings.routing.outputs[0];
    for(const auto& d:e.devices)if(d.name==e.settings.routing.outputs[0])output="A1  ·  "+d.description;
    box(p,{x+2,69,412,27},QColor("#3d5664"),line,2);
    text(p,{x+10,69,397,27},QFontMetrics(QFont("DejaVu Sans",9)).elidedText(output,Qt::ElideRight,390),9,e.settings.sink.isEmpty()?ink:green);
    add({x+2,69,412,27},Output,"Choose the output connected to your headphones. Does not enable monitoring.");
    for(int i=0;i<4;++i){const bool selected=!e.settings.routing.outputs[i+1].isEmpty();button(p,{x+2+i*104,104,99,22},QString("A%1  ·  %2").arg(i+2).arg(selected?"Selected":"Select"),selected,true,7);add({x+2+i*104,104,99,22},Output,QString("Select hardware output A%1").arg(i+2),i+1);}
    const auto& tape=*e.recorder;const auto& options=e.settings.tape;
    box(p,{x+2,141,250,128},dark,line,2);
    box(p,{x+9,147,205,29},QColor("#bbc5ab"),line,1);
    const QString title=tape.recording||tape.finalizing?QFileInfo(tape.recordFile).fileName():tape.file.isEmpty()?"Click to select audio file":QFileInfo(tape.file).fileName();
    text(p,{x+14,148,194,14},QFontMetrics(QFont("DejaVu Sans",7)).elidedText(title,Qt::ElideMiddle,190),7,dark);
    text(p,{x+14,162,194,12},tape.recording?(tape.recordPaused?"REC PAUSED":"RECORDING"):tape.finalizing?"Finalizing…":tape.playing?(tape.playPaused?"PAUSED":"PLAYING"):"Right-click for recording options",7,dark);add({x+9,147,205,29},TapeLoad,"Load a media file; right-click anywhere on the tape for recording options");
    meter(p,{x+10,181,25,27},tape.peak,true);
    for(int k=0;k<2;++k){p.setPen(QPen(line,1.5));p.setBrush(bg);p.drawEllipse(QPointF(x+61+k*103,194),11,9);p.drawLine(QPointF(x+53+k*103,194),QPointF(x+69+k*103,194));}
    auto time=[](double seconds){int t=std::max(0,int(seconds));return QString("%1:%2:%3").arg(t/3600,2,10,QChar('0')).arg(t/60%60,2,10,QChar('0')).arg(t%60,2,10,QChar('0'));};
    text(p,{x+76,183,76,22},time(tape.position),8,tape.recording?coral:ink,Qt::AlignCenter);
    text(p,{x+180,181,34,27},options.busMode?(options.bus<5?QString("A%1").arg(options.bus+1):QString("B%1").arg(options.bus-4)):"INPUT",7,coral,Qt::AlignCenter);
    box(p,{x+10,212,204,10},QColor("#23423a"),line,1);const double progress=tape.duration>0?std::clamp(tape.position/tape.duration,0.,1.):0;p.fillRect(QRectF(x+11,213,202*progress,8),green);add({x+10,211,204,12},TapeSeek,"Click to seek within the loaded file");
    text(p,{x+10,223,204,12},tape.error.isEmpty()?QString("%1 Hz · %2 Ch · %3 %4").arg(options.rate).arg(options.busMode?options.channels:2).arg(options.format,time(tape.duration)):QFontMetrics(QFont("DejaVu Sans",7)).elidedText(tape.error,Qt::ElideRight,202),7,tape.error.isEmpty()?dim:coral,Qt::AlignCenter);
    for(int i=0;i<5;++i){const QRectF r(x+10+i*41,239,38,23);const bool enabled=i!=4||tape.inputReady||tape.recording;button(p,r,(i==2&&tape.playing&&!tape.playPaused)||(i==4&&tape.recording&&!tape.recordPaused)?"Ⅱ":QStringList{"◀◀","▶▶","▶","■","●"}[i],i==2?tape.playing:i==4?tape.recording:false,enabled,9);if(enabled)add(r,TapeTransport,QStringList{"Rewind (hold)","Fast forward (hold)","Play / pause","Stop playback / finalize recording","Record / pause recording in the same file"}[i],i);}
    for(int bus=0;bus<8;++bus){const QRectF r(x+219,147+bus*14,28,12);button(p,r,bus<5?QString("A%1").arg(bus+1):QString("B%1").arg(bus-4),options.playback[bus],true,7);add(r,TapeRoute,"Route recorder playback to this bus",bus);}
    box(p,{x+263,141,151,128},panel,line,2);text(p,{x+270,150,137,20},"SPECIAL FX",8,dim,Qt::AlignCenter);
    button(p,{x+273,183,60,27},"Reverb",e.settings.routing.reverbEnabled,true,8);button(p,{x+340,183,64,27},"Delay",e.settings.routing.delayEnabled,true,8);
    add({x+273,183,60,27},FxOptions,"Reverb parameters and per-bus returns",0);add({x+340,183,64,27},FxOptions,"Delay time, feedback and per-bus returns",1);
    text(p,{x+272,220,135,40},"Strip sends → bus returns\nClick effect to configure",7,dim,Qt::AlignCenter);
    text(p,{x+2,289,412,25},"MASTER SECTION",10,ink,Qt::AlignCenter);
    for(int db:{12,0,-12,-24,-36,-48,-60}){const double y=524+(12-db)/72.*98;text(p,{x-11,y-6,15,12},QString::number(db),6,dim,Qt::AlignRight|Qt::AlignVCenter);}
    for(int i=0;i<8;++i) {
        const double xx=x+2+i*52;const QString label=i<5?QString("A%1").arg(i+1):QString("B%1").arg(i-4);
        const bool active=true;
        const QColor accent=i<5?green:amber;
        p.fillRect(QRectF(xx+4,342,40,2),accent);
        text(p,{xx,322,48,18},label,10,active?accent:dim,Qt::AlignCenter,true);
        button(p,{xx+2,348,44,32},e.settings.routing.busComposite[i]?"Composite\n8 Ch":"Normal\nStereo",e.settings.routing.busComposite[i],true,7);add({xx+2,348,44,32},BusMode,"Normal stereo or an 8-channel pre-fader Composite patch",i);
        text(p,{xx+2,383,44,12},"BALANCE",7,dim,Qt::AlignCenter);const double balance=e.settings.routing.busBalance[i];knob(p,{xx+24,409},16,(balance+1)/2,balance==0?"C":QString(balance<0?"L%1":"R%1").arg(std::abs(balance)*100,0,'f',0),!e.settings.routing.busComposite[i],true);if(!e.settings.routing.busComposite[i])add({xx+7,392,34,34},Balance,"Balance: drag sideways or up/down; scroll to adjust; double-click to center",i);
        button(p,{xx+3,442,42,20},e.settings.routing.busMono[i]?"mono":"stereo",e.settings.routing.busMono[i],!e.settings.routing.busComposite[i],7);if(!e.settings.routing.busComposite[i])add({xx+3,442,42,20},BusMono,"Stereo / mono downmix",i);button(p,{xx+3,468,42,20},"Mute",e.settings.routing.busMute[i],true,7);
        meter(p,{xx+3,506,15,134},e.matrix->busPeak[i],e.matrix->ready);
        fader(p,{xx+22,506,22,134},e.settings.routing.busGain[i],true,"Bus Gain",false,accent);
        add({xx+15,498,37,150},MasterGain,"Independent output bus gain",i);add({xx+3,468,42,20},MasterMute,"Mute this output bus only. From a terminal: linux-mixer --ctl mute a1..a5|b1..b3 toggle",i);
        text(p,{xx-1,650,51,22},i>=5?"VIRTUAL":"PHYSICAL",7,active?accent:dim,Qt::AlignCenter);
    }
}
void Console::chooseDevice(bool input,int bus) {
    QMenu menu(this);bool any=false;
    for(const auto& device:e.devices)if(device.input==input) {
        any=true;
        if(input){
            auto* choices=menu.addMenu(device.description);
            QStringList channels{"MONO"};for(const auto& channel:device.channels)if(!channels.contains(channel))channels.append(channel);
            for(const auto& channel:channels){auto* a=choices->addAction(channel=="MONO"?"Mono / downmix":channel);a->setCheckable(true);const auto& hw=e.settings.hardware(bus);a->setChecked(hw.mic==device.name&&hw.channel==channel);
                connect(a,&QAction::triggered,this,[this,device,bus,channel]{auto& hw=e.settings.hardware(bus);hw.mic=device.name;hw.channel=channel;e.save();e.start();});}
        }else{
            auto* a=menu.addAction(device.description);a->setCheckable(true);a->setChecked(device.name==e.settings.routing.outputs[bus]);
            connect(a,&QAction::triggered,this,[this,device,bus]{e.matrix->setOutput(bus,device.name);update();});
        }
    }
    menu.addSeparator();menu.addAction("Unassigned",this,[this,input,bus]{if(input){e.settings.hardware(bus).mic.clear();e.save();e.start();}else e.matrix->setOutput(bus,{});update();});
    if(!any)menu.addAction("No devices available")->setEnabled(false);
    menu.exec(QCursor::pos());
}
void Console::applications(int input){
    QMenu menu(this);
    if(e.matrix->apps.empty())menu.addAction("Play audio in an app to list it here")->setEnabled(false);
    for(const auto& app:e.matrix->apps){
        if(input>=0){
            auto* action=menu.addAction(app.name);action->setCheckable(true);action->setChecked(app.input==input);
            connect(action,&QAction::triggered,this,[this,key=app.key,input]{e.matrix->assignApp(key,input);});
        }else{
            auto* destinations=menu.addMenu(app.name);
            for(int i=0;i<3;++i){auto* a=destinations->addAction(virtualInputDescription(i));a->setCheckable(true);a->setChecked(app.input==i);connect(a,&QAction::triggered,this,[this,key=app.key,i]{e.matrix->assignApp(key,i);});}
            destinations->addSeparator();destinations->addAction("System default (forget assignment)",this,[this,key=app.key]{e.matrix->assignApp(key,-1);});
        }
    }
    if(!e.settings.routing.appRules.empty()){
        menu.addSeparator();auto* saved=menu.addMenu("Saved assignments");
        const auto rules=e.settings.routing.appRules;
        for(auto it=rules.begin();it!=rules.end();++it){const QString key=it.key();saved->addAction(key.section(':',1)+" → "+virtualInputDescription(it.value().toInt())+" (forget)",this,[this,key]{e.matrix->assignApp(key,-1);});}
    }
    menu.exec(QCursor::pos());
}
void Console::timing(int index){
    auto& hw=e.settings.hardware(index);
    QDialog d(this);styleMixerDialog(d);d.setMinimumWidth(280);d.setWindowTitle("Gate timing");auto* layout=new QFormLayout(&d);
    QDoubleSpinBox attack,release;attack.setRange(.1,500);attack.setValue(hw.attack);attack.setSuffix(" ms");release.setRange(.1,500);release.setValue(hw.release);release.setSuffix(" ms");
    layout->addRow("Attack",&attack);layout->addRow("Release",&release);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addRow(&buttons);
    connect(&buttons,&QDialogButtonBox::accepted,&d,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);
    if(d.exec()==QDialog::Accepted){hw.attack=attack.value();hw.release=release.value();e.apply();}
}
void Console::options(){
    QMenu m(this);
    m.addAction(isMaximized()?"Restore window":"Maximize window",this,[this]{
        const bool maximize=!isMaximized();
        if(setDesktopMaximized)setDesktopMaximized(maximize);
        else if(maximize)showMaximized();else showNormal();
    });
    m.addSeparator();m.addAction("Tape recorder options…",this,[this]{recorderOptions();});m.addAction("Replay tape from start",this,[this]{e.recorder->seek(0);if(!e.recorder->playing)e.recorder->play();});m.addAction("Eject tape",this,[this]{e.recorder->eject();});
    m.addAction("Gate attack / release…",this,[this]{timing();});
    m.addAction("Reset color panel",this,[this]{e.settings.colorX=e.settings.colorY=.5;e.apply();});
    m.addSeparator();
    m.addAction("Back up complete profile…",this,[this]{e.exportPreset(QFileDialog::getSaveFileName(this,"Back up complete mixer profile",{},"Mixer profile / legacy preset (*.json)"));});
    m.addAction("Restore profile / preset…",this,[this]{const auto f=QFileDialog::getOpenFileName(this,"Restore mixer profile / preset",{},"Mixer profile / legacy preset (*.json)");if(!f.isEmpty())e.importPreset(f);});
    m.addSeparator();m.addAction("Audio health…",this,[this]{audioHealth();});m.addAction("Check for updates…",this,[this]{updates->showDialog(this);});
    m.addSeparator();m.addAction("Restart audio engine",this,[this]{e.start();});m.addAction("Stop audio engine",this,[this]{e.stop();});
    m.addSeparator();m.addAction("Hide mixer (keep routing in background)",this,[this]{window()->close();});
    m.addAction("Quit mixer and stop audio",this,[this]{e.stop();closeToBackground={};QApplication::quit();});
    m.exec(QCursor::pos());
}
void Console::activate(Action a,const QPointF& pos,int first,int second) {
    auto& hw=e.settings.hardware(std::clamp(first,0,4));
    switch(a){
    case TapeLoad:{auto path=QFileDialog::getOpenFileName(this,"Load tape",{},"Audio / video (*.wav *.bwf *.aiff *.aif *.mp3 *.m4a *.mp4 *.mov *.avi *.wma *.wmv *.flac *.ogg);;All files (*)");if(!path.isEmpty())e.recorder->loadFile(path);break;}
    case TapeOptions:recorderOptions();break;
    case TapeSeek:e.recorder->seek(std::clamp((pos.x()-1058)/204.,0.,1.)*e.recorder->duration);break;
    case TapeRoute:if(first<5&&e.settings.routing.outputs[first].isEmpty())chooseDevice(false,first);else{e.settings.tape.playback[first]=!e.settings.tape.playback[first];e.apply();}break;
    case TapeTransport:
        if(first==0||first==1){transportDirection=first==0?-1:1;e.recorder->seek(e.recorder->position+transportDirection*2);transportTimer->start();}
        else if(first==2)e.recorder->play();else if(first==3)e.recorder->stop();else e.recorder->record();
        break;
    case Plugins:e.plugins->open(first);break;
    case BusMode:busOptions(first);break;
    case FxOptions:fxOptions(first);break;
    case BusMono:e.settings.routing.busMono[first]=!e.settings.routing.busMono[first];e.apply();break;
    case FxSend:dragging=FxSend;dragIndex=first;dragBand=second;dragStart=pos;dragValue=second==0?e.settings.routing.reverbSend[first]:second==1?e.settings.routing.fxAmount[first]:e.settings.routing.delaySend[first];break;
    case Balance:dragging=Balance;dragIndex=first;dragStart=pos;dragAxis=0;dragValue=e.settings.routing.busBalance[first];break;
    case Eq:dragging=Eq;dragIndex=first;dragBand=second;dragStart=pos;dragValue=e.settings.routing.eq[first][second];break;
    case Mic:chooseDevice(true,std::max(0,first));break;
    case Output:chooseDevice(false,std::max(0,first));break;
    case Menu:options();break;
    case Health:audioHealth();break;
    case Apps:applications(first);break;
    case Start:e.start();break;
    case Denoise:hw.denoise=!hw.denoise;e.apply();break;
    case Bypass:hw.bypass=!hw.bypass;e.apply();break;
    case Route:
        if(second<5 && e.settings.routing.outputs[second].isEmpty())chooseDevice(false,second);
        else e.matrix->setRoute(first,second,!e.settings.routing.routes[first][second]);
        break;
    case Mute:hw.mute=!hw.mute;e.apply();break;
    case MasterMute:e.settings.routing.busMute[first]=!e.settings.routing.busMute[first];e.apply();break;
    case VirtualMute:e.settings.routing.inputMute[first]=!e.settings.routing.inputMute[first];e.apply();break;
    case Color:case Gain:case VirtualGain:case MasterGain:case Comp:case Gate:
        dragging=a;dragStart=pos;dragIndex=std::max(0,first);
        dragValue=a==Comp?hw.compression:hw.threshold;
        if(a==Color || a==Gain || a==VirtualGain || a==MasterGain)drag(pos);
        break;
    default:break;
    }
    update();
}
void Console::drag(const QPointF& p) {
    auto& hw=e.settings.hardware(std::clamp(dragIndex,0,4));
    switch(dragging){
    case FxSend:{auto& value=dragBand==0?e.settings.routing.reverbSend[dragIndex]:dragBand==1?e.settings.routing.fxAmount[dragIndex]:e.settings.routing.delaySend[dragIndex];value=std::round(std::clamp(dragValue+(dragStart.y()-p.y())*.01,0.,1.)*100)/100;break;}
    case Balance:{const QPointF delta=p-dragStart;if(dragAxis==0&&std::max(std::abs(delta.x()),std::abs(delta.y()))>=1)dragAxis=std::abs(delta.x())>=std::abs(delta.y())?1:2;e.settings.routing.busBalance[dragIndex]=std::round(std::clamp(dragValue+(dragAxis==2?-delta.y():delta.x())*.015,-1.,1.)*100)/100;break;}
    case Eq:e.settings.routing.eq[dragIndex][dragBand]=std::round(std::clamp(dragValue+(dragStart.y()-p.y())*.2,-12.,12.)*10)/10;break;
    case Color:if(hw.positionPanel){e.settings.routing.hardwarePan[dragIndex]=std::clamp((p.x()-24-dragIndex*129)/92.*2-1,-1.,1.);const double depth=std::clamp((p.y()-186)/39.,-1.,1.);e.settings.routing.hardwareDepth[dragIndex]=std::abs(depth)<.08?0.:depth;}else{hw.colorX=std::clamp((p.x()-24-dragIndex*129)/92.,0.,1.);hw.colorY=std::clamp((p.y()-165)/48.,0.,1.);}break;
    case Gain:hw.gain=std::round(std::clamp(12-(p.y()-483)/139.*72,-60.,12.)*2)/2;break;
    case VirtualGain:e.settings.routing.inputGain[dragIndex]=std::round(std::clamp(12-(p.y()-483)/139.*72,-60.,12.)*2)/2;break;
    case MasterGain:e.settings.routing.busGain[dragIndex]=std::round(std::clamp(12-(p.y()-524)/98.*72,-60.,12.)*2)/2;break;
    case Gate:hw.threshold=std::round(std::clamp(dragValue+(dragStart.y()-p.y())*.3,-60.,0.));break;
    case Comp:hw.compression=std::round(std::clamp(dragValue+(dragStart.y()-p.y())*.05,0.,10.)*10)/10;break;
    default:return;
    }
    e.apply();
}
void Console::mousePressEvent(QMouseEvent* ev){
    if(ev->button()!=Qt::LeftButton)return;
    const auto pos=logical(ev->position());if(const auto* h=hit(pos))activate(h->action,pos,h->first,h->second);
}
void Console::mouseMoveEvent(QMouseEvent* ev){
    const auto pos=logical(ev->position());
    if(dragging!=None)drag(pos);
    else if(const auto* h=hit(pos)){setCursor(h->action==Color?Qt::CrossCursor:Qt::PointingHandCursor);QToolTip::showText(ev->globalPosition().toPoint(),h->tip,this);}
    else {unsetCursor();QToolTip::hideText();}
}
void Console::mouseReleaseEvent(QMouseEvent* ev){if(ev->button()!=Qt::LeftButton)return;transportTimer->stop();dragging=None;e.save();}
void Console::mouseDoubleClickEvent(QMouseEvent* ev){
    if(ev->button()!=Qt::LeftButton)return;
    if(const auto* h=hit(logical(ev->position()))){
        if(h->action==Balance)e.settings.routing.busBalance[h->first]=0;
        else if(h->action==FxSend)(h->second==0?e.settings.routing.reverbSend[h->first]:h->second==1?e.settings.routing.fxAmount[h->first]:e.settings.routing.delaySend[h->first])=h->second==1?1:0;
        else if(h->action==Eq)e.settings.routing.eq[h->first][h->second]=0;
        else if(h->action==Color){auto& hw=e.settings.hardware(h->first);if(hw.positionPanel){e.settings.routing.hardwarePan[h->first]=0;e.settings.routing.hardwareDepth[h->first]=0;}else hw.colorX=hw.colorY=.5;}
        else if(h->action==Gain)e.settings.hardware(h->first).gain=0;
        else if(h->action==MasterGain)e.settings.routing.busGain[h->first]=0;
        else if(h->action==VirtualGain)e.settings.routing.inputGain[h->first]=0;
        else return;
        e.apply();
    }
}
void Console::wheelEvent(QWheelEvent* ev){
    const auto* h=hit(logical(ev->position()));if(!h)return;const int delta=ev->angleDelta().y()!=0?ev->angleDelta().y():ev->angleDelta().x();if(delta==0)return;const double step=delta>0?1:-1;
    if(h->action==Balance)e.settings.routing.busBalance[h->first]=std::clamp(e.settings.routing.busBalance[h->first]+step*.05,-1.,1.);
    else if(h->action==FxSend){auto& v=h->second==0?e.settings.routing.reverbSend[h->first]:h->second==1?e.settings.routing.fxAmount[h->first]:e.settings.routing.delaySend[h->first];v=std::clamp(v+step*.05,0.,1.);}
    else if(h->action==Eq)e.settings.routing.eq[h->first][h->second]=std::clamp(e.settings.routing.eq[h->first][h->second]+step*.5,-12.,12.);
    else if(h->action==Gain)e.settings.hardware(h->first).gain=std::clamp(e.settings.hardware(h->first).gain+step*.5,-60.,12.);
    else if(h->action==VirtualGain)e.settings.routing.inputGain[h->first]=std::clamp(e.settings.routing.inputGain[h->first]+step*.5,-60.,12.);
    else if(h->action==MasterGain)e.settings.routing.busGain[h->first]=std::clamp(e.settings.routing.busGain[h->first]+step*.5,-60.,12.);
    else if(h->action==Gate)e.settings.hardware(h->first).threshold=std::clamp(e.settings.hardware(h->first).threshold+step,-60.,0.);
    else if(h->action==Comp)e.settings.hardware(h->first).compression=std::clamp(e.settings.hardware(h->first).compression+step*.1,0.,10.);
    else return;
    e.apply();
}
void Console::contextMenuEvent(QContextMenuEvent* ev){const auto pos=logical(ev->pos());if(QRectF(1050,141,250,128).contains(pos)){recorderOptions();return;}const auto* h=hit(pos);if(h && h->action==Color){dragging=None;auto& hw=e.settings.hardware(h->first);hw.positionPanel=!hw.positionPanel;e.save();update();}else if(h && h->action==Plugins){e.settings.pluginRacks[h->first]=false;e.plugins->sync(e.running);e.save();e.message="Input plugin rack bypassed; click Plugins to enable it again";update();}else if(h && h->action==Gate)timing(h->first);else options();}
