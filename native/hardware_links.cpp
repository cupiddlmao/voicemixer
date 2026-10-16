#include "hardware_links.h"
#include <QtCore/QHash>
QList<QPair<int,int>> hardwareLinks(const QJsonArray& graph,const Settings& settings,const QString& prefix){
    QHash<QString,int> nodes;struct Port{int id,node;QString channel,direction;};QList<Port> ports;
    for(const auto& value:graph){const auto item=value.toObject(),info=item.value("info").toObject(),props=info.value("props").toObject();
        if(item.value("type")=="PipeWire:Interface:Node")nodes.insert(props.value("node.name").toString(),item.value("id").toInt());
        if(item.value("type")=="PipeWire:Interface:Port")ports.append({item.value("id").toInt(),props.value("node.id").toInt(),props.value("audio.channel").toString(),info.value("direction").toString()});
    }
    QList<QPair<int,int>> links;
    for(int i=0;i<5;++i){const auto& hw=settings.hardware(i);if(hw.mic.isEmpty()||!nodes.contains(hw.mic))continue;
        QString name=captureName(i);name.replace("linux_mixer",prefix);const int source=nodes.value(hw.mic),capture=nodes.value(name,-1);
        for(const auto& in:ports)if(in.node==capture&&in.direction=="input"){
            for(const auto& out:ports)if(out.node==source&&out.direction=="output" && (hw.channel=="MONO"?(out.channel=="MONO"||out.channel=="FL"||out.channel=="FR"):out.channel==hw.channel))links.append({out.id,in.id});
        }
    }
    return links;
}
