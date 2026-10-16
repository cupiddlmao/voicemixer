#include "paths.h"
#include "recorder.h"
#include "routing_settings.h"
#include <QtCore/QJsonArray>
#include <QtCore/QStringList>
#include <algorithm>
#include <cmath>

RoutingSettings::RoutingSettings(){fxAmount.fill(1);reverbReturn.fill(1);delayReturn.fill(1);for(auto& patch:compositePatch)for(int i=0;i<8;++i)patch[i]=i;routes[0][5]=true;for(int i=5;i<8;++i)routes[i][0]=true;}
QJsonObject RoutingSettings::json()const{
    QJsonArray out,ig,im,bg,bm,rt;
    for(const auto& v:outputs)out.append(v);
    for(auto v:inputGain)ig.append(v);
    for(auto v:inputMute)im.append(v);
    for(auto v:busGain)bg.append(v);
    for(auto v:busMute)bm.append(v);
    for(const auto& row:routes){QJsonArray r;for(auto v:row)r.append(v);rt.append(r);}
    QJsonArray eqs,composite,post,patches,mono,balance;for(const auto& row:eq){QJsonArray a;for(double v:row)a.append(v);eqs.append(a);}for(int i=0;i<8;++i){composite.append(busComposite[i]);post.append(busCompositePost[i]);mono.append(busMono[i]);balance.append(busBalance[i]);QJsonArray a;for(int v:compositePatch[i])a.append(v);patches.append(a);}
    auto array=[](const auto& values){QJsonArray a;for(auto v:values)a.append(v);return a;};
    return {{"reverbSend",array(reverbSend)},{"delaySend",array(delaySend)},{"fxAmount",array(fxAmount)},{"reverbReturn",array(reverbReturn)},{"delayReturn",array(delayReturn)},{"reverbEnabled",reverbEnabled},{"delayEnabled",delayEnabled},{"room",room},{"damping",damping},{"delayMs",delayMs},{"feedback",feedback},{"eq",eqs},{"busComposite",composite},{"busCompositePost",post},{"compositePatch",patches},{"busMono",mono},{"busBalance",balance},{"outputs",out},{"inputGain",ig},{"inputMute",im},{"busGain",bg},{"busMute",bm},{"routes",rt},{"appRules",appRules}};
}
void RoutingSettings::load(const QJsonObject& o){
    auto numbers=[](auto& target,const QJsonArray& source){for(int i=0;i<int(target.size()) && i<source.size();++i){const double v=source[i].toDouble(target[i]);if(std::isfinite(v))target[i]=std::clamp(v,-60.,12.);}};
    auto flags=[](auto& target,const QJsonArray& source){for(int i=0;i<int(target.size()) && i<source.size();++i)if(source[i].isBool())target[i]=source[i].toBool();};
    const std::array<const char*,5> fxNames{"reverbSend","delaySend","fxAmount","reverbReturn","delayReturn"};const std::array<std::array<double,8>*,5> fxValues{&reverbSend,&delaySend,&fxAmount,&reverbReturn,&delayReturn};for(int i=0;i<5;++i){numbers(*fxValues[i],o.value(fxNames[i]).toArray());for(auto& v:*fxValues[i])v=std::clamp(v,0.,1.);}

    reverbEnabled=o.value("reverbEnabled").toBool(reverbEnabled);delayEnabled=o.value("delayEnabled").toBool(delayEnabled);
    auto number=[&](const char* key,double old,double lo,double hi){const double v=o.value(key).toDouble(old);return std::isfinite(v)?std::clamp(v,lo,hi):old;};room=number("room",room,0,1);damping=number("damping",damping,0,.98);delayMs=number("delayMs",delayMs,1,2000);feedback=number("feedback",feedback,0,.85);
    flags(busCompositePost,o.value("busCompositePost").toArray());flags(busComposite,o.value("busComposite").toArray());flags(busMono,o.value("busMono").toArray());numbers(busBalance,o.value("busBalance").toArray());for(auto& v:busBalance)v=std::clamp(v,-1.,1.);
    const auto eqs=o.value("eq").toArray();for(int i=0;i<3&&i<eqs.size();++i){numbers(eq[i],eqs[i].toArray());for(auto& v:eq[i])v=std::clamp(v,-12.,12.);}
    const auto patches=o.value("compositePatch").toArray();for(int i=0;i<8&&i<patches.size();++i){auto a=patches[i].toArray();for(int c=0;c<8&&c<a.size();++c)compositePatch[i][c]=std::clamp(a[c].toInt(),0,15);}
    const auto out=o.value("outputs").toArray();for(int i=0;i<5 && i<out.size();++i)if(out[i].isString())outputs[i]=out[i].toString();
    numbers(inputGain,o.value("inputGain").toArray());flags(inputMute,o.value("inputMute").toArray());
    numbers(busGain,o.value("busGain").toArray());flags(busMute,o.value("busMute").toArray());
    const auto rt=o.value("routes").toArray();for(int i=0;i<8 && i<rt.size();++i)flags(routes[rt.size()==4 && i>0?i+4:i],rt[i].toArray());
    if(o.value("appRules").isObject()){appRules={};
        const auto rules=o.value("appRules").toObject();for(auto it=rules.begin();it!=rules.end();++it)if(it.value().isDouble() && it.value().toInt(-1)>=0 && it.value().toInt(-1)<3)appRules.insert(it.key(),it.value().toInt());}
}
QString virtualInputName(int i,const QString& prefix){return prefix+QStringList{".system",".music",".aux"}.at(i);}
QString virtualInputDescription(int i){return QStringList{"Linux Mixer System","Linux Mixer Music","Linux Mixer AUX"}.at(i);}
QString busCaptureName(int i,const QString& prefix){return prefix+QString(".bus%1.capture").arg(i);}
QString busOutputName(int i,const QString& prefix){if(i<5)return prefix+QString(".bus%1.output").arg(i);return prefix+(i==5?".microphone":i==6?".b2":".b3");}

QJsonObject matrixConfiguration(const RoutingSettings& s,const QString& prefix,const RecorderSettings* tape){
    auto filter=[](const QString& name,const QString& label,QJsonObject controls=QJsonObject{}){if(label=="linear"||label=="mixer")return QJsonObject{{"type","ladspa"},{"plugin",pluginPath("libmixer_dsp.so")},{"name",name},{"label",label=="linear"?"smooth_gain":"smooth_mixer8"},{"control",controls}};return QJsonObject{{"type","builtin"},{"name",name},{"label",label},{"control",controls}};};
    QJsonArray modules{
        QJsonObject{{"name","libpipewire-module-rt"},{"flags",QJsonArray{"ifexists","nofail"}}},
        QJsonObject{{"name","libpipewire-module-protocol-native"}},QJsonObject{{"name","libpipewire-module-client-node"}},QJsonObject{{"name","libpipewire-module-adapter"}}};
    for(int i=0;i<3;++i){
        const double gain=s.inputMute[i]?0.:std::pow(10,s.inputGain[i]/20);
        QJsonArray eqNodes,eqLinks,eqInputs,eqOutputs;
        for(int c=0;c<2;++c){const QString side=c==0?"left":"right";QString previous;
            for(int band=0;band<3;++band){const QString name=side+QString("Eq%1").arg(band);const QString label=band==0?"bq_highshelf":band==1?"bq_peaking":"bq_lowshelf";
                eqNodes.append(filter(name,label,{{"Freq",band==0?6500:band==1?1000:120},{"Q",.707},{"Gain",s.eq[i][c*3+band]}}));
                if(band==0)eqInputs.append(name+":In");else eqLinks.append(QJsonObject{{"output",previous+":Out"},{"input",name+":In"}});previous=name;
            }
            eqNodes.append(filter(side,"linear",{{"Mult",gain},{"Add",0}}));eqLinks.append(QJsonObject{{"output",previous+":Out"},{"input",side+":In"}});eqOutputs.append(side+":Out");
        }
        const QJsonObject graph{{"nodes",eqNodes},{"links",eqLinks},{"inputs",eqInputs},{"outputs",eqOutputs}};
        const QJsonObject args{{"node.description",virtualInputDescription(i)},{"audio.rate",48000},{"audio.channels",2},{"audio.position",QJsonArray{"FL","FR"}},{"filter.graph",graph},
            {"capture.props",QJsonObject{{"node.name",virtualInputName(i,prefix)},{"node.description",virtualInputDescription(i)},{"media.class","Audio/Sink"},{"node.virtual",true},{"priority.session",0}}},
            {"playback.props",QJsonObject{{"node.name",prefix+QString(".input%1.output").arg(i)},{"node.description",virtualInputDescription(i)+" processing"},{"node.autoconnect",false},{"node.passive",true}}}};
        modules.append(QJsonObject{{"name","libpipewire-module-filter-chain"},{"args",args}});
    }
    for(int bus=0;bus<8;++bus){
        QJsonArray nodes,links,inputs,positions;
        for(int c=0;c<18;++c){const QString n=QString("input%1").arg(c);nodes.append(filter(n,"copy"));inputs.append(n+":In");positions.append(QString("AUX%1").arg(c));
            if(c<16)links.append(QJsonObject{{"output",n+":Out"},{"input",QString(c%2?"right:In %1":"left:In %1").arg(c/2+1)}});}
        QJsonObject controls;for(int input=0;input<8;++input)controls.insert(QString("Gain %1").arg(input+1),s.routes[input][bus]?1.:0.);
        nodes.append(filter("left","mixer",controls));nodes.append(filter("right","mixer",controls));
        for(int effect=0;effect<2;++effect){QJsonObject send;for(int input=0;input<8;++input)send.insert(QString("Gain %1").arg(input+1),s.routes[input][bus]?s.fxAmount[input]*(effect==0&&s.reverbEnabled?s.reverbSend[input]:effect==1&&s.delayEnabled?s.delaySend[input]:0.):0.);
            for(int c=0;c<2;++c){const QString mix=QString(effect==0?"verb%1":"echo%1").arg(c);nodes.append(filter(mix,"mixer",send));for(int input=0;input<8;++input)links.append(QJsonObject{{"output",QString("input%1:Out").arg(input*2+c)},{"input",mix+QString(":In %1").arg(input+1)}});}
        }
        nodes.append(QJsonObject{{"type","ladspa"},{"plugin",pluginPath("libmixer_dsp.so")},{"name","fx"},{"label","stereo_fx"},{"control",QJsonObject{{"Room",s.room},{"Damping",s.damping},{"Delay ms",s.delayMs},{"Feedback",s.feedback},{"Reverb return",s.reverbEnabled?s.reverbReturn[bus]:0.},{"Delay return",s.delayEnabled?s.delayReturn[bus]:0.}}}});
        for(int c=0;c<2;++c){links.append(QJsonObject{{"output",QString("verb%1:Out").arg(c)},{"input",QString(c==0?"fx:Reverb L":"fx:Reverb R")}});links.append(QJsonObject{{"output",QString("echo%1:Out").arg(c)},{"input",QString(c==0?"fx:Delay L":"fx:Delay R")}});}
        const double gain=s.busMute[bus]?0.:std::pow(10,s.busGain[bus]/20);
        nodes.append(filter("gainL","linear",{{"Mult",gain*(1-std::max(0.,s.busBalance[bus]))},{"Add",0}}));nodes.append(filter("gainR","linear",{{"Mult",gain*(1+std::min(0.,s.busBalance[bus]))},{"Add",0}}));
        const double tapeGain=tape&&tape->playback[bus]?std::pow(10,tape->gain/20):0;
        nodes.append(filter("tapeL","mixer",{{"Gain 1",1},{"Gain 2",tapeGain},{"Gain 3",1}}));nodes.append(filter("tapeR","mixer",{{"Gain 1",1},{"Gain 2",tapeGain},{"Gain 3",1}}));
        links.append(QJsonObject{{"output","left:Out"},{"input","tapeL:In 1"}});links.append(QJsonObject{{"output","right:Out"},{"input","tapeR:In 1"}});
        links.append(QJsonObject{{"output","input16:Out"},{"input","tapeL:In 2"}});links.append(QJsonObject{{"output","input17:Out"},{"input","tapeR:In 2"}});
        links.append(QJsonObject{{"output","fx:Out L"},{"input","tapeL:In 3"}});links.append(QJsonObject{{"output","fx:Out R"},{"input","tapeR:In 3"}});
        for(int c=0;c<2;++c){const auto name=c==0?"monoL":"monoR";nodes.append(filter(name,"mixer",{{"Gain 1",s.busMono[bus]?.5:c==0?1.:0.},{"Gain 2",s.busMono[bus]?.5:c==0?0.:1.}}));links.append(QJsonObject{{"output","tapeL:Out"},{"input",QString(name)+":In 1"}});links.append(QJsonObject{{"output","tapeR:Out"},{"input",QString(name)+":In 2"}});links.append(QJsonObject{{"output",QString(name)+":Out"},{"input",c==0?"gainL:In":"gainR:In"}});}

        nodes.append(QJsonObject{{"type","ladspa"},{"plugin",pluginPath("libmixer_dsp.so")},{"name","limiter"},{"label","stereo_limiter"},{"control",QJsonObject{{"Ceiling",-1}}}});
        links.append(QJsonObject{{"output","gainL:Out"},{"input","limiter:In L"}});links.append(QJsonObject{{"output","gainR:Out"},{"input","limiter:In R"}});
        QJsonArray graphOutputs{"limiter:Out L","limiter:Out R"};
        if(s.busComposite[bus]){
            nodes={};links={};inputs={};positions={};graphOutputs={};
            for(int c=0;c<18;++c){const auto name=QString("input%1").arg(c);nodes.append(filter(name,"copy"));inputs.append(name+":In");positions.append(QString("AUX%1").arg(c));}
            for(int pair=0;pair<4;++pair){const auto limit=QString("compositeLimit%1").arg(pair);nodes.append(QJsonObject{{"type","ladspa"},{"plugin",pluginPath("libmixer_dsp.so")},{"name",limit},{"label","stereo_limiter"},{"control",QJsonObject{{"Ceiling",-1}}}});
                for(int side=0;side<2;++side){const int c=pair*2+side;const auto name=QString("compositeGain%1").arg(c);nodes.append(filter(name,"linear",{{"Mult",gain},{"Add",0}}));
                    const auto mix=QString("compositeTape%1").arg(c);nodes.append(filter(mix,"mixer",{{"Gain 1",1},{"Gain 2",c<2?tapeGain:0.}}));links.append(QJsonObject{{"output",QString("input%1:Out").arg(s.compositePatch[bus][c])},{"input",mix+":In 1"}});if(c<2)links.append(QJsonObject{{"output",QString("input%1:Out").arg(16+c)},{"input",mix+":In 2"}});
                    links.append(QJsonObject{{"output",mix+":Out"},{"input",name+":In"}});links.append(QJsonObject{{"output",name+":Out"},{"input",limit+(side?":In R":":In L")}});graphOutputs.append(limit+(side?":Out R":":Out L"));}
            }
        }
        const QJsonArray outputPositions=s.busComposite[bus]?QJsonArray{"FL","FR","FC","LFE","RL","RR","SL","SR"}:QJsonArray{"FL","FR"};
        QJsonObject playback{{"node.name",busOutputName(bus,prefix)},{"node.autoconnect",false},{"priority.session",0},{"node.virtual",true},
            {"audio.channels",s.busComposite[bus]?8:2},{"audio.position",outputPositions}};
        if(bus>=5){playback.insert("media.class","Audio/Source");playback.insert("node.description",bus==5?"Linux Mixer Microphone":QString("Linux Mixer B%1").arg(bus-4));}
        else playback.insert("node.description",QString("Linux Mixer A%1 output").arg(bus+1));
        QJsonObject args{{"node.description",QString("Linux Mixer bus %1").arg(bus)},{"audio.rate",48000},
            {"filter.graph",QJsonObject{{"nodes",nodes},{"links",links},{"inputs",inputs},{"outputs",graphOutputs}}},
            {"capture.props",QJsonObject{{"node.name",busCaptureName(bus,prefix)},{"linux.mixer.matrix.version",4},{"node.autoconnect",false},{"stream.dont-remix",true},{"audio.channels",18},{"audio.position",positions}}},
            {"playback.props",playback}};
        modules.append(QJsonObject{{"name","libpipewire-module-filter-chain"},{"args",args}});
    }
    return {{"context.properties",QJsonObject{{"log.level",2}}},{"context.spa-libs",QJsonObject{{"audio.convert.*","audioconvert/libspa-audioconvert"},{"support.*","support/libspa-support"}}},{"context.modules",modules}};
}
