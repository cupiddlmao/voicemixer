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

#include "ui_theme.h"
#include <chrono>
namespace {
using namespace theme;
constexpr double W=1480,H=720;
// Shared geometry: paint code and hit-testing/drag code both read these, so a
// control cannot move visually without moving its grab region with it.
constexpr double page=16,pitch=129,stripW=117,top=52,stripH=624;
constexpr double padTop=top+82,padW=101,padH=88,padInX=13.5,padInY=14;   // IntelliPan / EQ block
// padInX is 13.5, not 12, because the pad's travel box must stay 74px wide with
// its left edge at x+21.5: the project's pointer tests (native/smoke.cpp) press
// at logical x=93 on strip 1 and require pan to land on exactly 0.5, and x=24 /
// x=116 must clamp to -1 / +1. Paint and drag both read padInner().
constexpr double gainTop=top+392,gainH=190,capH=36;                       // strip fader zone
constexpr double masterX=1048,masterW=416,mfTop=506,mfH=138;              // master fader zone
constexpr double seekX=1056,seekW=196,tapeY=142,tapeW=262,tapeH=144;      // tape block
constexpr double fxX=masterX+274,fxW=142;                                // special FX block
constexpr double busPitch=50,busCol=46,busTop=326;                        // master section columns
double stripX(int i){return page+i*pitch;}
QRectF padRect(int i){return {stripX(i)+8,padTop,padW,padH};}
QRectF padInner(int i){const auto r=padRect(i);return r.adjusted(padInX,padInY,-padInX,-padInY);}
// Three-up compact knobs (EQ bands, FX sends): 30px on a 35px pitch fills the
// 101px content column of a strip exactly, so both strip kinds agree on x.
constexpr double triplePitch=35;
double tripleX(int i,int k){return stripX(i)+8+knobCompact/2+k*triplePitch;}
double faderDb(double y,double zoneTop,double zoneH){return 12-std::clamp((y-(zoneTop+capH/2))/(zoneH-capH),0.,1.)*72;}
QPointF g_hover(-1,-1),g_press(-1,-1);
QColor alpha(QColor c,int a){c.setAlpha(a);return c;}
double scaleOf(QPainter& p){const auto t=p.worldTransform();return std::min(t.m11(),t.m22());}
void text(QPainter& p,QRectF r,const QString& s,int px=type::value,QColor c=ink,int align=Qt::AlignLeft|Qt::AlignVCenter,bool bold=false,bool caps=false) {
    // Keep glyphs proportional when the canvas is stretched.
    p.save();const auto t=p.worldTransform();
    const double sx=std::hypot(t.m11(),t.m12()),sy=std::hypot(t.m21(),t.m22());
    const double scale=std::min(sx,sy);const QPointF origin=t.map(r.topLeft());
    p.setWorldTransform(QTransform(t.m11()/sx*scale,t.m12()/sx*scale,
        t.m21()/sy*scale,t.m22()/sy*scale,origin.x(),origin.y()));
    p.setFont(font(px,bold,caps));p.setPen(c);
    p.drawText(QRectF(0,0,r.width()*sx/scale,r.height()*sy/scale),align,s);p.restore();
}
QString elide(const QString& s,int px,double width,Qt::TextElideMode m=Qt::ElideRight){return QFontMetrics(font(px)).elidedText(s,m,int(width));}
void caption(QPainter& p,QRectF r,const QString& s,int align=Qt::AlignLeft|Qt::AlignVCenter,QColor c=inkMuted){text(p,r,s.toUpper(),type::micro,c,align,true,true);}
void panelBox(QPainter& p,QRectF r,QColor fill=panel,QColor edge=border,double radius=theme::radius){p.setPen(QPen(edge,1));p.setBrush(fill);p.drawRoundedRect(r.adjusted(.5,.5,-.5,-.5),radius,radius);}
void dashedBox(QPainter& p,QRectF r){QPen pen(borderStrong,1,Qt::DashLine);p.setPen(pen);p.setBrush(Qt::NoBrush);p.drawRoundedRect(r.adjusted(.5,.5,-.5,-.5),controlRadius,controlRadius);}
// tone: colour used when active. outline: unfilled style (routing buttons).
void button(QPainter& p,QRectF r,const QString& s,bool active=false,bool enabled=true,int px=type::value,QColor tone=accent,bool outline=false) {
    const bool hover=enabled&&r.contains(g_hover),pressed=hover&&r.contains(g_press);
    QColor fill=active?tone:outline?QColor(Qt::transparent):panelRaised,edge=active?tone:outline?borderStrong:border;
    if(active){if(pressed)fill=fill.darker(120);else if(hover)fill=fill.lighter(112);}
    else if(enabled){if(pressed){fill=bg;edge=tone;}else if(hover){fill=outline?panelRaised:fill.lighter(125);edge=borderStrong;}}
    if(!enabled)edge=border;
    panelBox(p,r,fill,edge,controlRadius);
    text(p,r,s,px,!enabled?inkMuted:active?bg:ink,Qt::AlignCenter,active);
}
// A control that is on by default (denoise, an effect send) is state, not signal:
// it keeps the accent hue but as an outline and tint, so a row of five of them
// no longer out-shouts the metering. Solid accent stays for genuinely active
// routing and for the engine running. Off, hover and pressed stay distinct.
void stateButton(QPainter& p,QRectF r,const QString& s,bool on,bool enabled=true,int px=type::micro,QColor tone=accent) {
    const bool hover=enabled&&r.contains(g_hover),pressed=hover&&r.contains(g_press);
    QColor fill=on?alpha(tone,26):panelRaised,edge=on?alpha(tone,140):border;
    if(!enabled){fill=panelRaised;edge=border;}
    else if(pressed){fill=on?alpha(tone,64):bg;edge=tone;}
    else if(hover){fill=on?alpha(tone,44):panelRaised.lighter(125);edge=on?tone:borderStrong;}
    panelBox(p,r,fill,edge,controlRadius);
    text(p,r,s,px,!enabled?inkMuted:on?tone:ink,Qt::AlignCenter,on);
}
// Eight recorder-route chips live on 21x18 cells, four to a column, so the A
// group wraps into the second column and A5 ends up beside B1. The cells cannot
// move, so the groups are separated by the hue the rest of the console already
// uses for buses: A is accent, B is warn. The tint stays faint at rest and only
// fills solid when the route is running.
void groupChip(QPainter& p,QRectF r,const QString& s,bool on,QColor tone) {
    const bool hover=r.contains(g_hover),pressed=hover&&r.contains(g_press);
    QColor fill=on?tone:alpha(tone,24),edge=on?tone:alpha(tone,120);
    if(pressed){fill=on?tone.darker(120):alpha(tone,64);edge=tone;}
    else if(hover){fill=on?tone.lighter(112):alpha(tone,48);edge=tone;}
    panelBox(p,r,fill,edge,controlRadius);
    text(p,r,s,type::micro,on?bg:ink,Qt::AlignCenter,on);
}
void knob(QPainter& p,QPointF c,double d,double amount,const QString& value,bool enabled=true,bool bipolar=false) {
    const double sc=scaleOf(p),t11=p.worldTransform().m11(),t22=p.worldTransform().m22();
    // Same instrument at two diameters: identical 1.5px ring gap, identical 3px
    // stroke and 270-degree sweep, and a value that sits one step down the type
    // ramp on the compact 30px knob so its glyphs clear the ring as the 44px
    // knob's do. That is what makes 44 and 30 read as a family rather than as a
    // full-size control next to a squeezed one.
    const double rx=d/2*sc/t11,ry=d/2*sc/t22,inx=3*sc/t11,iny=3*sc/t22;
    p.setPen(Qt::NoPen);p.setBrush(bg);p.drawEllipse(c,rx,ry);
    const QRectF arc(c.x()-rx+inx,c.y()-ry+iny,2*(rx-inx),2*(ry-iny));
    p.setBrush(Qt::NoBrush);p.setPen(QPen(track,3,Qt::SolidLine,Qt::RoundCap));p.drawArc(arc,225*16,-270*16);
    if(enabled){
        p.setPen(QPen(accent,3,Qt::SolidLine,Qt::RoundCap));
        const int span=-int(270*16*(bipolar?amount-.5:amount));
        if(span!=0)p.drawArc(arc,bipolar?90*16:225*16,span);
    }
    const int px=d>=knobSize?type::value:type::micro;
    text(p,QRectF(c.x()-d/2,c.y()-8,d,16),value,px,enabled?ink:inkMuted,Qt::AlignCenter);
}
double holdPeak(int id,double db){
    static double hold[24],until[24],last[24];
    const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const double dt=std::min(now-last[id],.5);last[id]=now;
    if(db>=hold[id]){hold[id]=db;until[id]=now+1.2;}
    else if(now>until[id])hold[id]=std::max(db,hold[id]-25*dt);
    return hold[id];
}
// 4px vertical meter with peak-hold line.
// At rest the slot keeps a visible well and a floor anchor, so a silent console
// reads as meters sitting at the bottom rather than as dead dark columns. The
// live paint below is deliberately unchanged, and the rect is the same rect, so
// no fader moves.
void meter(QPainter& p,QRectF r,double db,bool live,int id) {
    if(!live){
        p.setPen(Qt::NoPen);p.setBrush(meterWell);p.drawRoundedRect(r,2,2);
        if(r.height()>r.width()){p.setBrush(borderStrong);p.drawRoundedRect(QRectF(r.x(),r.bottom()-4,r.width(),4),2,2);}
        holdPeak(id,-90);return;
    }
    p.setPen(Qt::NoPen);p.setBrush(bg);p.drawRoundedRect(r,2,2);
    auto level=[&](double v){return std::clamp((v+60)/60.,0.,1.);};
    const double h=r.height()*level(db);
    p.save();p.setClipRect(r);
    const double yWarn=r.bottom()-r.height()*level(-12),yDanger=r.bottom()-r.height()*level(-3);
    p.fillRect(QRectF(r.x(),r.bottom()-h,r.width(),h),accent);
    if(r.bottom()-h<yWarn)p.fillRect(QRectF(r.x(),r.bottom()-h,r.width(),yWarn-(r.bottom()-h)),warn);
    if(r.bottom()-h<yDanger)p.fillRect(QRectF(r.x(),r.bottom()-h,r.width(),yDanger-(r.bottom()-h)),danger);
    p.restore();
    const double ph=holdPeak(id,db);
    if(ph>-60){const double y=r.bottom()-r.height()*level(ph);p.fillRect(QRectF(r.x()-1,y-1,r.width()+2,1.5),ph>=-3?danger:ink);}
}
void fader(QPainter& p,QRectF r,double value,bool live,bool drawScale=true,QColor tone=accent) {
    const double cx=r.center().x(),t=std::clamp((12-value)/72.,0.,1.);
    const double yy=r.y()+capH/2+t*(r.height()-capH);
    p.setPen(Qt::NoPen);p.setBrush(meterWell);p.drawRoundedRect(QRectF(cx-4,r.y(),8,r.height()),4,4);
    // Travel already covered is position, not signal. Holding it at full accent
    // put thirteen long bright bars on screen at unity - more green than the
    // meters it is supposed to sit beside, which is what still made the console
    // read loud after the toggles were quieted. Keep the hue (A still green, B
    // still amber) so the columns stay distinguishable, at a strength below the
    // meter: the moving thing stays the brightest thing.
    // 130, not 110: at 110 the amber B-bus travel reads brown at rest, which
    // cost the columns their identity. Mint at 110 still reads green, so this
    // is the point where amber stops going muddy while staying under the meter.
    p.setBrush(live?alpha(tone,130):track);p.drawRoundedRect(QRectF(cx-4,yy,8,r.bottom()-yy),4,4);
    for(int db:{0,-24,-48}) {
        const double y=r.y()+capH/2+(12-db)/72.*(r.height()-capH);
        // The master bank has 50px per column and a 26px cap, which leaves 12px of
        // clear width - not enough for "-24" at the smallest type - so the master
        // scale is read from weighted marks instead: unity is the long, bright
        // dash, and the section header names all three (see masters()).
        const bool unity=!drawScale&&db==0;
        p.setPen(QPen(unity?inkMuted:borderStrong,1));
        p.drawLine(QPointF(cx-(unity?20:18),y),QPointF(cx-14,y));
        if(drawScale)text(p,QRectF(r.x()-4,y-7,24,14),QString::number(db),type::micro,inkMuted,Qt::AlignRight|Qt::AlignVCenter);
    }
    const QRectF cap(cx-13,yy-capH/2,26,capH);
    panelBox(p,cap,panelRaised.lighter(118),live?tone:borderStrong,controlRadius);
    p.setPen(QPen(borderStrong,1));p.drawLine(QPointF(cx-8,yy-3),QPointF(cx+8,yy-3));p.drawLine(QPointF(cx-8,yy+3),QPointF(cx+8,yy+3));
}
}
void styleMixerDialog(QDialog& dialog){
    QPalette colors=dialog.palette();
    colors.setColor(QPalette::Window,bg);colors.setColor(QPalette::WindowText,ink);
    colors.setColor(QPalette::Base,bg);colors.setColor(QPalette::AlternateBase,panel);
    colors.setColor(QPalette::Text,ink);colors.setColor(QPalette::Button,panelRaised);colors.setColor(QPalette::ButtonText,ink);
    colors.setColor(QPalette::Highlight,accentDim);colors.setColor(QPalette::HighlightedText,ink);
    colors.setColor(QPalette::ToolTipBase,panelRaised);colors.setColor(QPalette::ToolTipText,ink);
    colors.setColor(QPalette::PlaceholderText,inkMuted);
    for(auto role:{QPalette::Text,QPalette::WindowText,QPalette::ButtonText})colors.setColor(QPalette::Disabled,role,inkMuted);
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
    // Header: title left, controls right-aligned to the page margin.
    const double hy=14,hh=rowHeight;
    p.setPen(Qt::NoPen);p.setBrush(accent);p.drawRoundedRect(QRectF(page,hy+2,24,24),controlRadius,controlRadius);
    text(p,{page,hy+2,24,24},"LM",type::value,bg,Qt::AlignCenter,true);
    text(p,{page+36,hy,150,hh},"Linux Mixer",type::title,ink,Qt::AlignLeft|Qt::AlignVCenter,true);
    text(p,{page+190,hy,220,hh},"Personal audio console",type::value,inkMuted);
    const QRectF menuR(1368,hy,96,hh),healthR(1224,hy,132,hh),startR(1116,hy,96,hh),appsR(996,hy,108,hh);
    button(p,appsR,"Applications");add(appsR,Apps,"Assign running apps to System, Music or AUX; choices are remembered");
    button(p,startR,e.running?"Engine on":"Start",e.running);add(startR,Start,"Start or restart the audio engine with the selected microphone");
    {const bool known=e.health->available&&e.health->rate>0&&e.health->periodMs>0;
     button(p,healthR,known?QString("%1 Hz · %2 ms").arg(e.health->rate).arg(e.health->periodMs,0,'f',1):"48 000 Hz · Stereo",false,known);
     if(known)add(healthR,Health,"Driver sample rate and scheduling period (not total latency). Click for audio health");}
    {bool anyMic=false;for(int i=0;i<5;++i)anyMic|=!e.settings.hardware(i).mic.isEmpty();
     if(!e.running){const QString guide=!anyMic?"1 Pick a mic  ·  2 Pick headphones  ·  3 Start":e.settings.routing.outputs[0].isEmpty()?"2 Pick headphones (A1)  ·  3 Start":"3 Press Start";
        text(p,{430,hy,550,hh},guide,type::value,inkSecondary,Qt::AlignCenter);}}
    button(p,menuR,updates->update.version.isEmpty()?"Menu":"Menu · Update",false,true,type::value,updates->update.version.isEmpty()?accent:warn);add(menuR,Menu,"Presets, gate timing, stop engine and quit");
    for(int i=0;i<8;++i)strip(p,i,i<5);
    masters(p);
    // Status bar: quiet dot + text, right-aligned health affordance.
    const double sy=684;
    p.setBrush(e.running?accent:inkMuted);p.setPen(Qt::NoPen);p.drawEllipse(QPointF(page+4,sy+10),3,3);
    text(p,{page+16,sy,900,20},elide(e.message,type::value,890),type::value,inkSecondary);
    QString healthText="Audio health…";if(e.health->available)healthText=QString("Audio health · %1 errors").arg(e.health->errors);
    const QRectF hr(1164,sy,300,20);text(p,hr,healthText,type::value,e.health->errors?danger:inkSecondary,Qt::AlignRight|Qt::AlignVCenter);add(hr,Health,"Audio buffer timing, processing load, node errors and plugin recovery");
}
void Console::strip(QPainter& p,int index,bool hardware) {
    const double x=stripX(index);const bool live=hardware,virtualInput=!hardware,usable=true;const int vi=index-5;const auto& s=e.settings.hardware(hardware?index:0);const auto& routing=e.settings.routing;
    const double y=top;
    panelBox(p,{x,y,stripW,stripH});
    caption(p,{x+8,y+8,stripW-16,16},hardware?QString("Input %1").arg(index+1):QString("Virtual %1").arg(index-4),Qt::AlignLeft|Qt::AlignVCenter,inkSecondary);
    // device chip
    const QRectF chip(x+8,y+28,101,rowHeight);
    const bool unset=live&&s.mic.isEmpty();
    QString name="Select mic";
    if(live) {if(!unset)name=s.mic;for(const auto& d:e.devices)if(d.name==s.mic)name=d.description;}
    // The node description is "Linux Mixer System"; the strip caption already says
    // VIRTUAL n, so the chip shows the word that actually differs.
    else name=QString(virtualInputDescription(vi)).section(' ',1);
    if(unset)dashedBox(p,chip);else{panelBox(p,chip,panelRaised,chip.contains(g_hover)?borderStrong:border,controlRadius);}
    text(p,chip.adjusted(8,0,-8,0),elide(name,type::value,chip.width()-16),type::value,unset?inkMuted:ink);
    if(live)add(chip,Mic,"Select physical microphone or interface channel. Changing devices restarts the engine.",index);
    if(virtualInput)add(chip,Apps,"Assign running apps to this input",vi);
    if(hardware) {
        caption(p,{x+8,y+62,stripW-16,16},s.positionPanel?"Position":"Voice");
        const QRectF pad=padRect(index),in=padInner(index);
        panelBox(p,pad,bg,border,controlRadius);
        p.setPen(QPen(grid,1));
        for(int k=1;k<4;++k){p.drawLine(QPointF(in.x()+in.width()*k/4,pad.y()+3),QPointF(in.x()+in.width()*k/4,pad.bottom()-3));p.drawLine(QPointF(pad.x()+3,in.y()+in.height()*k/4),QPointF(pad.right()-3,in.y()+in.height()*k/4));}
        p.setPen(QPen(border,1));p.drawLine(QPointF(in.center().x(),pad.y()+3),QPointF(in.center().x(),pad.bottom()-3));p.drawLine(QPointF(pad.x()+3,in.center().y()),QPointF(pad.right()-3,in.center().y()));
        const int mid=Qt::AlignHCenter|Qt::AlignVCenter;
        if(s.positionPanel){text(p,{pad.x(),pad.y()+1,pad.width(),13},"Front",type::micro,inkMuted,mid);text(p,{pad.x(),pad.bottom()-14,pad.width(),13},"Back",type::micro,inkMuted,mid);
            text(p,{pad.x()+2,in.center().y()-7,14,14},"L",type::micro,inkMuted,mid);text(p,{pad.right()-16,in.center().y()-7,14,14},"R",type::micro,inkMuted,mid);}
        else{text(p,{pad.x(),pad.y()+1,pad.width(),13},"Presence",type::micro,inkMuted,mid);
            text(p,{pad.x()+4,pad.bottom()-14,50,13},"Warm",type::micro,inkMuted,Qt::AlignLeft|Qt::AlignVCenter);text(p,{pad.right()-54,pad.bottom()-14,50,13},"Bright",type::micro,inkMuted,Qt::AlignRight|Qt::AlignVCenter);}
        const QPointF pos(in.x()+(s.positionPanel?(routing.hardwarePan[index]+1)*.5:s.colorX)*in.width(),s.positionPanel?in.center().y()+std::clamp(routing.hardwareDepth[index],-1.,1.)*in.height()/2:in.y()+s.colorY*in.height());
        p.setPen(QPen(live?accent:borderStrong,1.5));p.setBrush(live?accentDim:track);p.drawEllipse(pos,5,5);
        if(live)add(pad,Color,s.positionPanel?"Drag to position this input: left/right pans, up/down moves it front (louder, closer) or back (quieter, more reverb). Double-click to center. Right-click for Color Panel.":"Drag to shape warmth, brightness and presence. Double-click to reset. Right-click for left/right position.",index);
    } else {
        caption(p,{x+8,y+62,stripW-16,16},"Equalizer");
        for(int k=0;k<3;++k)text(p,{tripleX(index,k)-triplePitch/2,y+82,triplePitch,14},QStringList{"Treble","Mid","Bass"}[k],type::micro,inkSecondary,Qt::AlignCenter);
        for(int k=0;k<6;++k){const QPointF center{tripleX(index,k%3),y+(k<3?114:154)};const double value=routing.eq[vi][k];knob(p,center,knobCompact,(value+12)/24,QString::number(value,'f',value==std::floor(value)?0:1),true,true);add({center.x()-16,center.y()-16,32,32},Eq,QString("%1 channel %2 EQ gain (dB); double-click for flat").arg(k<3?"Left":"Right",QStringList{"Treble","Mid","Bass"}[k%3]),vi,k);}
    }
    if(virtualInput){
        caption(p,{x+8,y+178,stripW-16,16},"Applications");
        int row=0;
        for(const auto& app:e.matrix->apps)if(app.input==vi){
            if(row==4)break;
            const QRectF r(x+8,y+196+row*22,101,20);button(p,r,elide(app.name,type::value,90));add(r,Apps,"Change app assignments",vi);++row;
        }
        if(row==0){const QRectF r(x+8,y+196,101,84);dashedBox(p,r);
            text(p,{r.x(),r.y()+20,r.width(),18},"No apps",type::value,inkSecondary,Qt::AlignCenter);text(p,{r.x(),r.y()+40,r.width(),14},"Click input name",type::micro,inkMuted,Qt::AlignCenter);}
    }
    if(hardware) {
        caption(p,{x+8,y+178,stripW-16,16},"Dynamics");
        text(p,{x+8,y+194,50,14},"Comp",type::value,inkSecondary,Qt::AlignCenter);text(p,{x+59,y+194,50,14},"Gate",type::value,inkSecondary,Qt::AlignCenter);
        const QPointF comp{x+33,y+232},gate{x+84,y+232};
        knob(p,comp,knobSize,s.compression/10,QString::number(s.compression,'f',1),live);
        knob(p,gate,knobSize,(s.threshold+60)/60,QString::number(s.threshold,'f',0),live);
        add({comp.x()-knobSize/2,comp.y()-knobSize/2,knobSize,knobSize},Comp,"Compressor amount. Drag vertically or use mouse wheel.",index);add({gate.x()-knobSize/2,gate.y()-knobSize/2,knobSize,knobSize},Gate,"Gate threshold in dB. Right-click for attack/release timing.",index);
        const QRectF denoise(x+8,y+260,49,22),bypass(x+60,y+260,49,22);
        stateButton(p,denoise,"Denoise",s.denoise,live);button(p,bypass,"Bypass",s.bypass,live,type::micro,warn);
        add(denoise,Denoise,"Toggle RNNoise noise suppression live",index);add(bypass,Bypass,"Bypass denoise, gate, compressor and color. Fader and mute stay active.",index);
    }
    {const QRectF r(x+8,y+288,101,24);button(p,r,e.plugins->hasFailed(index)?"FX error":"Plugins",e.plugins->running(index),true,type::value,e.plugins->hasFailed(index)?danger:accent);add(r,Plugins,"Show or hide this input’s VST3/LADSPA plugin rack without stopping effects; right-click to bypass the rack (Windows VST3 via yabridge)",index);}
    {for(int k=0;k<3;++k){const double cx=tripleX(index,k);const QPointF center{cx,y+350};
    text(p,{cx-triplePitch/2,y+318,triplePitch,14},QStringList{"Reverb","FX","Delay"}[k],type::micro,inkSecondary,Qt::AlignCenter);
        const double value=k==0?routing.reverbSend[index]:k==1?routing.fxAmount[index]:routing.delaySend[index];knob(p,center,knobCompact,value,QString::number(value*100,'f',0),true);add({center.x()-16,center.y()-16,32,32},FxSend,k==0?"Reverb send (%)":k==1?"FX send level (%)":"Delay send (%)",index,k);}}
    // fader zone: one rect feeds the painter, the scale labels and the grab region
    const QRectF faderR(x+14,gainTop,80,gainH),grab(faderR.center().x()-20,gainTop-8,40,gainH+16);
    const double gain=live?s.gain:virtualInput?routing.inputGain[vi]:0;
    meter(p,{x+8,gainTop,4,gainH},live?e.hardwarePeak[index]:virtualInput?e.matrix->inputPeak[vi]:-90,live?e.running:virtualInput && e.matrix->ready,index);
    fader(p,faderR,gain,usable);
    text(p,{x+8,y+372,50,16},gain<=-60?"−∞ dB":QString::number(gain,'f',gain==std::floor(gain)?0:1)+" dB",type::value,ink);
    if(live&&s.mic.isEmpty())text(p,{x+50,y+372,59,16},"No input",type::micro,inkMuted,Qt::AlignRight|Qt::AlignVCenter);
    else if(live)text(p,{x+50,y+372,59,16},e.hardwarePeak[index]>=-.1?"CLIP":QString::number(e.hardwarePeak[index],'f',1)+" dBFS",type::micro,e.hardwarePeak[index]>=-.1?danger:inkMuted,Qt::AlignRight|Qt::AlignVCenter);
    else text(p,{x+50,y+372,59,16},e.matrix->ready?"App level":"Engine off",type::micro,inkMuted,Qt::AlignRight|Qt::AlignVCenter);
    if(live)add(grab,Gain,"Microphone gain in dB. Double-click for 0 dB.",index);
    if(virtualInput)add(grab,VirtualGain,"App playback level. Double-click for 0 dB.",vi);
    for(int b=0;b<8;++b) {
        const int inputRow=index;const bool active=routing.routes[inputRow][b];
        const QString label=b<5?QString("A%1").arg(b+1):QString("B%1").arg(b-4);
        const QRectF r(x+74,gainTop+b*(routeHeight+2),35,routeHeight);
        button(p,r,label,active,true,type::value,b<5?accent:warn,true);
        add(r,Route,b<5?QString("Send this strip to hardware output A%1").arg(b+1):QString("Send this strip to virtual microphone B%1").arg(b-4),inputRow,b);
    }
    const double by=y+588;
    {const bool stereoPath=virtualInput||e.plugins->running(index);const QRectF r(x+8,by,49,rowHeight);panelBox(p,r,bg,border,controlRadius);text(p,r,virtualInput?"Stereo":e.plugins->running(index)?"Stereo FX":"Mono",type::micro,stereoPath?accent:inkSecondary,Qt::AlignCenter);}
    const QRectF mute(x+60,by,49,rowHeight);
    button(p,mute,"Mute",live?s.mute:virtualInput && routing.inputMute[vi],usable,type::value,danger);
    if(live)add(mute,Mute,"Mute this microphone output. From a terminal: linux-mixer --ctl mute mic<N> toggle",index);
    if(virtualInput)add(mute,VirtualMute,"Mute this virtual input. From a terminal: linux-mixer --ctl mute system|music|aux toggle",vi);
}
void Console::masters(QPainter& p) {
    const double x=masterX,y=top;
    // Hardware outputs are a group with its own controls, so they get the same
    // panel as a strip. The panel closes 4px above the tape block; the caption
    // and the A1..A5 rows keep the y they already had.
    panelBox(p,{x,y,masterW,tapeY-y-4});
    caption(p,{x+8,y+8,masterW,16},"Hardware out",Qt::AlignLeft|Qt::AlignVCenter,inkSecondary);
    QString output=e.settings.routing.outputs[0].isEmpty()?"Select headphone output":e.settings.routing.outputs[0];
    for(const auto& d:e.devices)if(d.name==e.settings.routing.outputs[0])output=d.description;
    const QRectF a1(x,y+28,masterW,rowHeight);const bool a1set=!e.settings.routing.outputs[0].isEmpty();
    if(a1set)panelBox(p,a1,panelRaised,a1.contains(g_hover)?borderStrong:border,controlRadius);else dashedBox(p,a1);
    text(p,{x+8,a1.y(),28,rowHeight},"A1",type::value,a1set?accent:inkMuted,Qt::AlignLeft|Qt::AlignVCenter,true);
    text(p,{x+36,a1.y(),masterW-44,rowHeight},elide(output,type::value,masterW-52),type::value,a1set?ink:inkMuted);
    add(a1,Output,"Choose the output connected to your headphones. Does not enable monitoring.");
    for(int i=0;i<4;++i){const bool selected=!e.settings.routing.outputs[i+1].isEmpty();const QRectF r(x+i*107,y+60,95,routeHeight);button(p,r,QString("A%1 · %2").arg(i+2).arg(selected?"Selected":"Select"),selected);add(r,Output,QString("Select hardware output A%1").arg(i+2),i+1);}
    // Tape / player
    const auto& tape=*e.recorder;const auto& options=e.settings.tape;
    panelBox(p,{x,tapeY,tapeW,tapeH});
    const QRectF file(x+8,tapeY+8,246,32);
    const bool hasFile=tape.recording||tape.finalizing||!tape.file.isEmpty();
    const QString title=tape.recording||tape.finalizing?QFileInfo(tape.recordFile).fileName():QFileInfo(tape.file).fileName();
    const QString state=tape.recording?(tape.recordPaused?"Rec paused":"Recording"):tape.finalizing?"Finalizing…":tape.playing?(tape.playPaused?"Paused":"Playing"):"Loaded · right-click for recording options";
    if(hasFile){panelBox(p,file,QColor(accent.red(),accent.green(),accent.blue(),36),accentDim,controlRadius);
        text(p,{file.x()+8,file.y()+2,file.width()-16,15},elide(title,type::value,file.width()-18,Qt::ElideMiddle),type::value,ink);
        text(p,{file.x()+8,file.y()+17,file.width()-16,13},state,type::micro,tape.recording?warn:accent);}
    else{dashedBox(p,file);text(p,{file.x()+8,file.y()+2,file.width()-16,15},"Load audio file",type::value,inkSecondary);text(p,{file.x()+8,file.y()+17,file.width()-16,13},"Right-click for recording options",type::micro,inkMuted);}
    add(file,TapeLoad,"Load a media file; right-click anywhere on the tape for recording options");
    auto time=[](double seconds){int t=std::max(0,int(seconds));return QString("%1:%2:%3").arg(t/3600,2,10,QChar('0')).arg(t/60%60,2,10,QChar('0')).arg(t%60,2,10,QChar('0'));};
    text(p,{x+8,tapeY+44,86,20},time(tape.position),type::label,tape.recording?warn:ink,Qt::AlignLeft|Qt::AlignVCenter,true);
    {const QRectF m(x+98,tapeY+52,64,4);meter(p,m,-90,false,16);
     const double level=std::clamp((tape.peak+60)/60.,0.,1.);p.fillRect(QRectF(m.x(),m.y(),m.width()*level,4),tape.peak>=-3?danger:accent);}
    text(p,{x+166,tapeY+44,38,20},options.busMode?(options.bus<5?QString("A%1").arg(options.bus+1):QString("B%1").arg(options.bus-4)):"Input",type::value,inkSecondary,Qt::AlignRight|Qt::AlignVCenter);
    const QRectF bar(seekX,tapeY+68,seekW,6);p.setPen(Qt::NoPen);p.setBrush(bg);p.drawRoundedRect(bar,3,3);
    const double progress=tape.duration>0?std::clamp(tape.position/tape.duration,0.,1.):0;p.setBrush(accent);p.drawRoundedRect(QRectF(bar.x(),bar.y(),std::max(progress>0?6.:0.,bar.width()*progress),6),3,3);
    add(bar.adjusted(0,-4,0,8),TapeSeek,"Click to seek within the loaded file");
    text(p,{seekX,tapeY+78,seekW,14},tape.error.isEmpty()?QString("%1 Hz · %2 ch · %3 %4").arg(options.rate).arg(options.busMode?options.channels:2).arg(options.format,time(tape.duration)):elide(tape.error,type::micro,seekW),type::micro,tape.error.isEmpty()?inkMuted:danger,Qt::AlignLeft|Qt::AlignVCenter);
    for(int i=0;i<5;++i){const QRectF r(seekX+i*40,tapeY+98,36,36);const bool enabled=i!=4||tape.inputReady||tape.recording;
        button(p,r,(i==2&&tape.playing&&!tape.playPaused)||(i==4&&tape.recording&&!tape.recordPaused)?"Ⅱ":QStringList{"◀◀","▶▶","▶","■","●"}[i],i==2?tape.playing:i==4?tape.recording:false,enabled,type::value,i==4?warn:accent);
        if(i==4&&enabled&&!tape.recording){p.setPen(Qt::NoPen);p.setBrush(warn);p.drawEllipse(r.center(),1.5,1.5);}
        if(enabled)add(r,TapeTransport,QStringList{"Rewind (hold)","Fast forward (hold)","Play / pause","Stop playback / finalize recording","Record / pause recording in the same file"}[i],i);}
    for(int bus=0;bus<8;++bus){const QRectF r(x+210+(bus/4)*23,tapeY+44+(bus%4)*22,21,18);groupChip(p,r,bus<5?QString("A%1").arg(bus+1):QString("B%1").arg(bus-4),options.playback[bus],bus<5?accent:warn);add(r,TapeRoute,"Route recorder playback to this bus",bus);}
    // Special FX
    const double fx=fxX;panelBox(p,{fx,tapeY,fxW,tapeH});caption(p,{fx+8,tapeY+8,fxW-16,16},"Special FX");
    const QRectF rev(fx+8,tapeY+30,fxW-16,rowHeight),del(fx+8,tapeY+66,fxW-16,rowHeight);
    stateButton(p,rev,"Reverb",e.settings.routing.reverbEnabled,true,type::value);stateButton(p,del,"Delay",e.settings.routing.delayEnabled,true,type::value);
    add(rev,FxOptions,"Reverb parameters and per-bus returns",0);add(del,FxOptions,"Delay time, feedback and per-bus returns",1);
    text(p,{fx+8,tapeY+96,fxW-16,36},"Click an effect to configure its bus returns",type::micro,inkMuted,Qt::AlignLeft|Qt::AlignTop|Qt::TextWordWrap);
    // Master section
    caption(p,{x,298,masterW,16},"Master section",Qt::AlignLeft|Qt::AlignVCenter,inkSecondary);
    text(p,{x+150,298,masterW-150,16},"0 · -24 · -48 dB",type::micro,inkMuted,Qt::AlignRight|Qt::AlignVCenter);
    panelBox(p,{x,316,masterW,360});
    for(int i=0;i<8;++i) {
        const double xx=x+10+i*busPitch;const QString label=i<5?QString("A%1").arg(i+1):QString("B%1").arg(i-4);
        const QColor tone=i<5?accent:warn;const auto& r=e.settings.routing;
        text(p,{xx,busTop,busCol,20},label,type::label,tone,Qt::AlignCenter,true);
        p.fillRect(QRectF(xx+(busCol-26)/2,busTop+22,26,2),tone);
        // "Stereo" used to appear twice in every column - once for the patch
        // format and once for the downmix directly below it - and neither told
        // you which was which. Name the format by channel count instead, so the
        // pair reads 2 ch/8 ch here and Stereo/Mono below.
        const QRectF mode(xx,busTop+28,busCol,routeHeight);button(p,mode,r.busComposite[i]?"8 ch":"2 ch",r.busComposite[i],true,type::micro,tone);add(mode,BusMode,"Normal stereo or an 8-channel pre-fader Composite patch",i);
        // Repeated eight times, so it is the section's quietest text: still a label
        // above its knob, one step below the knob's own value in weight.
        text(p,{xx,busTop+54,busCol,14},"Balance",type::micro,alpha(inkMuted,168),Qt::AlignCenter);
        const QPointF balanceCenter{xx+busCol/2,busTop+94};const double balance=r.busBalance[i];
        knob(p,balanceCenter,knobSize,(balance+1)/2,balance==0?"C":QString(balance<0?"L%1":"R%1").arg(std::abs(balance)*100,0,'f',0),!r.busComposite[i],true);
        const QRectF balanceHit{balanceCenter.x()-knobSize/2,balanceCenter.y()-knobSize/2,knobSize,knobSize};
        if(!r.busComposite[i])add(balanceHit,Balance,"Balance: drag sideways or up/down; scroll to adjust; double-click to center",i);
        const QRectF mono(xx,busTop+122,busCol,routeHeight),muteR(xx,busTop+150,busCol,routeHeight);
        button(p,mono,r.busMono[i]?"Mono":"Stereo",r.busMono[i],!r.busComposite[i],type::micro,tone);if(!r.busComposite[i])add(mono,BusMono,"Stereo / mono downmix",i);
        button(p,muteR,"Mute",r.busMute[i],true,type::value,danger);add(muteR,MasterMute,"Mute this output bus only. From a terminal: linux-mixer --ctl mute a1..a5|b1..b3 toggle",i);
        const QRectF faderR(xx+14,mfTop,busCol-16,mfH);
        meter(p,{xx+4,mfTop,4,mfH},e.matrix->busPeak[i],e.matrix->ready,8+i);
        fader(p,faderR,r.busGain[i],true,false,tone);
        add(QRectF(faderR.center().x()-16,mfTop-6,32,mfH+12),MasterGain,"Independent output bus gain",i);
        text(p,{xx,mfTop+mfH+4,busCol,16},r.busGain[i]<=-60?"−∞":QString::number(r.busGain[i],'f',r.busGain[i]==std::floor(r.busGain[i])?0:1),type::value,ink,Qt::AlignCenter);
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
    case TapeSeek:e.recorder->seek(std::clamp((pos.x()-seekX)/seekW,0.,1.)*e.recorder->duration);break;
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
    // Absolute-position controls read their value back out of the same rects the
    // painter used, so geometry can never drift apart between input and paint.
    case Color:{const auto in=padInner(dragIndex);const double fx=std::clamp((p.x()-in.x())/in.width(),0.,1.);
        if(hw.positionPanel){e.settings.routing.hardwarePan[dragIndex]=fx*2-1.;const double depth=std::clamp((p.y()-in.center().y())/(in.height()/2),-1.,1.);e.settings.routing.hardwareDepth[dragIndex]=std::abs(depth)<.08?0.:depth;}
        else{hw.colorX=fx;hw.colorY=std::clamp((p.y()-in.y())/in.height(),0.,1.);}break;}
    case Gain:hw.gain=std::round(std::clamp(faderDb(p.y(),gainTop,gainH),-60.,12.)*2)/2;break;
    case VirtualGain:e.settings.routing.inputGain[dragIndex]=std::round(std::clamp(faderDb(p.y(),gainTop,gainH),-60.,12.)*2)/2;break;
    case MasterGain:e.settings.routing.busGain[dragIndex]=std::round(std::clamp(faderDb(p.y(),mfTop,mfH),-60.,12.)*2)/2;break;
    case Gate:hw.threshold=std::round(std::clamp(dragValue+(dragStart.y()-p.y())*.3,-60.,0.));break;
    case Comp:hw.compression=std::round(std::clamp(dragValue+(dragStart.y()-p.y())*.05,0.,10.)*10)/10;break;
    default:return;
    }
    e.apply();
}
void Console::mousePressEvent(QMouseEvent* ev){
    if(ev->button()!=Qt::LeftButton)return;
    const auto pos=logical(ev->position());g_press=pos;
    if(const auto* h=hit(pos))activate(h->action,pos,h->first,h->second);
}
void Console::mouseMoveEvent(QMouseEvent* ev){
    const auto pos=logical(ev->position());
    // A menu opened from mousePressEvent runs its own event loop and eats the
    // release, so clear the pressed state on the first button-less move.
    if(dragging==None && ev->buttons()==Qt::NoButton && g_press.x()>=0){g_press={-1,-1};update();}
    if(pos!=g_hover){g_hover=pos;update();}   // drives hover/pressed styling
    if(dragging!=None)drag(pos);
    else if(const auto* h=hit(pos)){setCursor(h->action==Color?Qt::CrossCursor:Qt::PointingHandCursor);QToolTip::showText(ev->globalPosition().toPoint(),h->tip,this);}
    else {unsetCursor();QToolTip::hideText();}
}
void Console::mouseReleaseEvent(QMouseEvent* ev){if(ev->button()!=Qt::LeftButton)return;g_press={-1,-1};transportTimer->stop();dragging=None;e.save();}
void Console::leaveEvent(QEvent* ev){g_hover={-1,-1};g_press={-1,-1};QToolTip::hideText();update();QWidget::leaveEvent(ev);}
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
void Console::contextMenuEvent(QContextMenuEvent* ev){const auto pos=logical(ev->pos());if(QRectF(masterX,tapeY,tapeW,tapeH).contains(pos)){recorderOptions();return;}const auto* h=hit(pos);if(h && h->action==Color){dragging=None;auto& hw=e.settings.hardware(h->first);hw.positionPanel=!hw.positionPanel;e.save();update();}else if(h && h->action==Plugins){e.settings.pluginRacks[h->first]=false;e.plugins->sync(e.running);e.save();e.message="Input plugin rack bypassed; click Plugins to enable it again";update();}else if(h && h->action==Gate)timing(h->first);else options();}
