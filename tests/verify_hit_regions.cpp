// Hit-region reachability. The console paints itself and registers one rect per
// control, so a layout change can leave a control painted but unreachable: the
// rect registered later wins, because hit() scans back-to-front. The pointer
// tests in native/smoke.cpp press at absolute coordinates and only speak
// against a live PipeWire graph, so that class of regression had no offline
// guard. This is it, and it needs no audio: the engine is never started, only
// flagged, so paintEvent registers the live controls without touching a device.
#include "console.h"
#include "engine.h"
#include <QtWidgets/QApplication>
#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtCore/QTextStream>
#include <QtCore/QRectF>
#include <map>

static int failures=0;
static void fail(const QString& what){QTextStream(stderr)<<"FAIL: "<<what<<"\n";++failures;}

// Names for the log, so a failure says what went missing instead of a number.
// It lives inside the friend function because Console::Action is private.

int runHitRegionTest(Console& c){
    auto name=[](Console::Action a)->const char*{
        switch(a){
        case Console::None:return "None";case Console::Mic:return "Mic";case Console::Output:return "Output";
        case Console::Start:return "Start";case Console::Menu:return "Menu";case Console::Comp:return "Comp";
        case Console::Gate:return "Gate";case Console::Denoise:return "Denoise";case Console::Bypass:return "Bypass";
        case Console::Color:return "Color pad";case Console::Gain:return "Gain fader";case Console::MasterGain:return "Master fader";
        case Console::MasterMute:return "Master mute";case Console::VirtualGain:return "Virtual gain";
        case Console::VirtualMute:return "Virtual mute";case Console::Route:return "Route";case Console::Apps:return "Apps";
        case Console::Mute:return "Mute";case Console::Timing:return "Timing";case Console::ResetColor:return "Reset color";
        case Console::Eq:return "EQ";case Console::TapeLoad:return "Tape load";case Console::TapeSeek:return "Tape seek";
        case Console::TapeTransport:return "Tape transport";case Console::TapeRoute:return "Tape route";
        case Console::TapeOptions:return "Tape options";case Console::Plugins:return "Plugins";case Console::BusMode:return "Bus mode";
        case Console::FxSend:return "FX send";case Console::FxOptions:return "FX options";case Console::Balance:return "Balance";
        case Console::BusMono:return "Bus mono";case Console::Health:return "Health";
        case Console::MacroRun:return "Macro run";
        }
        return "?";
    };

    static constexpr double W=1480,H=720;
    const QRectF canvas(0,0,W,H);
    std::map<int,int> seen;

    // 1. Reachability and bounds. This is the half that catches a covered
    // control, and it needs no knowledge of what the layout is supposed to be.
    for(const auto& h:c.hits){
        ++seen[h.action];
        if(h.rect.width()<=0||h.rect.height()<=0){fail(QString("%1 registers an empty rect").arg(name(h.action)));continue;}
        if(!canvas.contains(h.rect)){
            fail(QString("%1 rect %2,%3 %4x%5 falls outside the 1480x720 canvas").arg(name(h.action))
                     .arg(h.rect.x()).arg(h.rect.y()).arg(h.rect.width()).arg(h.rect.height()));
            continue;
        }
        const auto* got=c.hit(h.rect.center());
        if(!got) fail(QString("%1 at %2,%3 %4x%5 hits nothing at all").arg(name(h.action))
                          .arg(h.rect.x()).arg(h.rect.y()).arg(h.rect.width()).arg(h.rect.height()));
        else if(got->action!=h.action||got->first!=h.first||got->second!=h.second)
            // Naming the thief is the actionable half: "Denoise is covered by Plugins #3".
            fail(QString("%1 at %2,%3 %4x%5 is covered by \"%6\" — painted, but the user cannot click it")
                     .arg(name(h.action)).arg(h.rect.x()).arg(h.rect.y()).arg(h.rect.width()).arg(h.rect.height())
                     .arg(QString(name(got->action))+(got->first>=0?QString(" #%1").arg(got->first):QString())));
    }

    auto hasAt=[&](Console::Action a,int idx){for(const auto& h:c.hits) if(h.action==a&&h.first==idx) return true;return false;};
    auto hasPair=[&](Console::Action a,int x,int y){for(const auto& h:c.hits) if(h.action==a&&h.first==x&&h.second==y) return true;return false;};
    auto count=[&](Console::Action a){auto it=seen.find(a);return it==seen.end()?0:it->second;};

    // 2. Inventory. Every control a user must be able to reach, per input, per
    // bus. The engine is flagged live above, so the live-only registrations
    // (Mic, Gain, Mute, Color pad) are expected here rather than skipped.
    for(int i=0;i<5;++i)
        for(auto a:{Console::Mic,Console::Comp,Console::Gate,Console::Denoise,Console::Bypass,Console::Plugins,
                    Console::Gain,Console::Mute,Console::Color})
            if(!hasAt(a,i)) fail(QString("hardware input %1 has no %2").arg(i+1).arg(name(a)));
    // Virtual strips carry their own 0-based index (vi = strip - 5) for gain,
    // mute and app assignment, but Plugins is keyed by strip, so it is 5..7.
    for(int i=0;i<3;++i){
        for(auto a:{Console::VirtualGain,Console::VirtualMute,Console::Apps})
            if(!hasAt(a,i)) fail(QString("virtual input %1 has no %2").arg(i+1).arg(name(a)));
        if(!hasAt(Console::Plugins,5+i)) fail(QString("virtual input %1 has no Plugins").arg(i+1));
    }
    // Routing is the console's whole point: every strip must reach every bus.
    for(int input=0;input<8;++input) for(int bus=0;bus<8;++bus)
        if(!hasPair(Console::Route,input,bus)) fail(QString("input %1 cannot route to bus %2").arg(input+1).arg(bus<5?QString("A%1").arg(bus+1):QString("B%1").arg(bus-4)));
    for(int i=0;i<8;++i){
        const bool composite=c.e.settings.routing.busComposite[i];
        for(auto a:{Console::MasterGain,Console::MasterMute}) if(!hasAt(a,i)) fail(QString("bus %1 has no %2").arg(i+1).arg(name(a)));
        if(!composite) for(auto a:{Console::BusMono,Console::Balance}) if(!hasAt(a,i)) fail(QString("bus %1 is not composite but has no %2").arg(i+1).arg(name(a)));
    }
    // A1 carries no index; A2..A5 are the four selectable slots.
    if(count(Console::Output)<5) fail(QString("expected 5 output selectors, found %1").arg(count(Console::Output)));
    for(auto a:{Console::Start,Console::Menu,Console::TapeSeek}) if(!count(a)) fail(QString("%1 is missing entirely").arg(name(a)));
    if(!count(Console::Health)) fail("no Audio health affordance");
    if(!hasAt(Console::FxOptions,0)||!hasAt(Console::FxOptions,1)) fail("Reverb and Delay must both be configurable");
    if(count(Console::TapeRoute)!=8) fail(QString("tape playback offers %1 route buttons, expected 8").arg(count(Console::TapeRoute)));
    if(count(Console::TapeTransport)<3) fail(QString("tape transport has %1 buttons, expected at least 3").arg(count(Console::TapeTransport)));

    QTextStream summary(stdout);
    summary<<c.hits.size()<<" hit rects across "<<seen.size()<<" action kinds\n";
    for(const auto& kv:seen) summary<<"  "<<name((Console::Action)kv.first)<<": "<<kv.second<<"\n";

    // 3. The pins in native/smoke.cpp, checked against the real hit list rather
    // than a model of it. smoke.cpp presses absolute logical pixels and its
    // tests only run against live audio, so a control that moves turns them
    // into clicks on nothing that nobody notices offline. Every coordinate it
    // presses must land on a control, and when the trailing comment names one,
    // it must land on that control.
    QRegularExpression pin("\\{\\s*(-?[0-9.]+)\\s*,\\s*(-?[0-9.]+)\\s*\\}");
    QFile smoke(QString(MIXER_SOURCE_DIR) + "/native/smoke.cpp");
    if(!smoke.open(QIODevice::ReadOnly|QIODevice::Text)){
        fail("cannot open native/smoke.cpp to check its pinned coordinates");
    } else {
        // Tags are written by the tests themselves: "click(c,{874.5,654}); //
        // music-mute: Music strip Mute ...". Keying on the tag beats guessing
        // from words, because "mute" alone matches mic, virtual and master mute.
        static const struct{const char* tag;Console::Action action;} tags[]{
            {"denoise",Console::Denoise},{"mic-mute",Console::Mute},{"music-mute",Console::VirtualMute},
            {"mono-b2",Console::BusMono},{"pad",Console::Color},{"pan-left",Console::Color},
            {"pan-right",Console::Color},{"pan-reset",Console::Color},{"route-b1",Console::Route},
            {"route-b2",Console::Route},{"route-b3",Console::Route},{"balance-loop",Console::Balance},
            {"seek",Console::TapeSeek},{"chip",Console::TapeRoute},{"transport",Console::TapeTransport},
            {"plugins",Console::Plugins},{"gate",Console::Gate},{"comp",Console::Comp},
        };
        const QString src=QString::fromUtf8(smoke.readAll());
        const auto lines=src.split('\n');
        QRegularExpression tagRe("//\\s*([a-z0-9_-]+)\\s*:");
        int checked=0,tagged=0;
        for(int ln=0;ln<lines.size();++ln){
            const QString& line=lines[ln];
            if(!line.contains("click(")&&!line.contains("QMouseEvent")) continue;
            auto it=pin.globalMatch(line);
            const auto tagMatch=tagRe.match(line);
            const QString tag=tagMatch.hasMatch()?tagMatch.captured(1):QString();
            while(it.hasNext()){
                const auto m=it.next();
                const double x=m.captured(1).toDouble(),y=m.captured(2).toDouble();
                if(x>1480||y>720) continue;                       // a size, not a position
                ++checked;
                const auto* got=c.hit(QPointF(x,y));
                if(!got){fail(QString("smoke.cpp:%1 presses %2,%3 and hits nothing").arg(ln+1).arg(x).arg(y));continue;}
                if(tag.isEmpty()){
                    fail(QString("smoke.cpp:%1 presses %2,%3 with no tag — write \"// name:\" so this pin can be checked").arg(ln+1).arg(x).arg(y));
                    continue;
                }
                bool known=false;
                for(const auto& t:tags){
                    if(tag!=t.tag) continue;
                    known=true;++tagged;
                    if(got->action!=t.action)
                        fail(QString("smoke.cpp:%1 pin \"%2\" at %3,%4 hits \"%5\" but the tag means \"%6\"")
                                 .arg(ln+1).arg(tag).arg(x).arg(y).arg(name(got->action)).arg(name(t.action)));
                    break;
                }
                // A tag nobody maps is a pin nobody checks.
                if(!known) fail(QString("smoke.cpp:%1 uses tag \"%2\", which this test does not know — add it to the table and say what control it names").arg(ln+1).arg(tag));
            }
        }
        QTextStream(stdout)<<"  "<<checked<<" pinned coordinates resolved against the live hit list, "<<tagged<<" checked by tag\n";
    }

    summary<<(failures?"":"no covered or off-canvas controls\n");
    return failures?1:0;
}

int main(int argc,char** argv){
    qputenv("QT_QPA_PLATFORM","offscreen");
    QApplication app(argc,argv);app.setStyle("Fusion");
    app.setApplicationName("Linux Mixer");app.setOrganizationName("PersonalAudio");
    Engine engine;
    // Flagged, never started: paintEvent keys the live controls off e.running,
    // and nothing below calls apply(), so no device and no graph is touched.
    engine.running=true;engine.connected=true;
    for(int i=0;i<5;++i) engine.settings.hardware(i).mic=QString("verify.input%1").arg(i);
    for(int i=0;i<5;++i) engine.settings.routing.routes[i][0]=true;
    Console console(engine);
    console.resize(1480,720);   // 1:1 with the logical canvas, the same units smoke.cpp presses in
    console.grab();             // paintEvent is what fills the hit list
    const int rc=runHitRegionTest(console);
    QTextStream(stdout)<<(rc==0?"PASS: every registered control is painted, in bounds and clickable\n":"FAIL\n");
    return rc;
}
