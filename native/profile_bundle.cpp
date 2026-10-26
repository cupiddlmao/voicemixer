#include "profile_bundle.h"
#include "engine.h"
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>
#include <QtCore/QDir>
#include <QtCore/QDateTime>
#include <QtCore/QUuid>
#include <QtCore/QXmlStreamReader>
#include <QtCore/QXmlStreamWriter>
#include <QtCore/QRegularExpression>
#include <cmath>
#include <functional>
#include <QtCore/QMap>

namespace {
bool settingsValid(const QJsonObject& settings,QString& error){
    const auto version=settings.value("version");
    if(!version.isDouble()||version.toDouble()!=version.toInt()||version.toInt()<1||version.toInt()>3){error="Unsupported mixer settings version";return false;}
    // Reject malformed types instead of letting permissive settings loaders
    // silently turn an invalid import into defaults. Unknown fields are rejected.
    const QStringList strings{"mic","channel","sink","directory","prefix","format"};
    const QStringList booleans{"denoise","bypass","mute","positionPanel","engineEnabled","broadcast","busMute","auxMute","auxA1","busMode","multitrack","playOnLoad","loop","reverbEnabled","delayEnabled"};
    const QStringList numbers{"version","threshold","attack","release","compression","colorX","colorY","gain","busGain","auxGain","bus","channels","bits","rate","kbps","bufferSeconds","preSeconds","stopAfter","room","damping","delayMs","feedback"};
    const QStringList booleanArrays{"pluginRacks","armed","playback","busComposite","busCompositePost","busMono","busMute","inputMute"};
    const QStringList numericArrays{"hardwarePan","reverbSend","delaySend","fxAmount","reverbReturn","delayReturn","inputGain","busGain","busBalance"};
    std::function<bool(const QJsonObject&)> check=[&](const QJsonObject& object){
        for(auto it=object.begin();it!=object.end();++it){
            const auto key=it.key();const auto value=it.value();
            if((key=="routing"||key=="tape")&&value.isObject()){if(!check(value.toObject()))return false;}
            else if(key=="hardware"&&value.isArray()){
                const auto a=value.toArray();if(a.size()!=5)return false;for(const auto& v:a)if(!v.isObject()||!check(v.toObject()))return false;
            }else if(key=="appRules"&&value.isObject()){
                const auto rules=value.toObject();if(rules.size()>1024)return false;for(auto r=rules.begin();r!=rules.end();++r)if(r.key().size()>1024||!r.value().isDouble()||r.value().toDouble()!=r.value().toInt()||r.value().toInt()<0||r.value().toInt()>2)return false;
            }else if(value.isArray()){
                const auto a=value.toArray();int expected=8;
                if(key=="hardwarePan"||key=="outputs")expected=5;
                if(key=="inputGain"||key=="inputMute"||key=="eq")expected=3;
                if(a.size()!=expected)return false;
                if(key=="routes"||key=="eq"||key=="compositePatch"){
                    const int width=key=="eq"?6:8;
                    for(const auto& row:a){if(!row.isArray()||row.toArray().size()!=width)return false;for(const auto& v:row.toArray()){
                        if(key=="routes"){if(!v.isBool())return false;}
                        else if(!v.isDouble()||!std::isfinite(v.toDouble())||std::abs(v.toDouble())>1000000)return false;
                        if(key=="compositePatch"&&(v.toDouble()!=v.toInt()||v.toInt()<0||v.toInt()>15))return false;
                    }}
                }else if(key=="outputs"){for(const auto& v:a)if(!v.isString()||v.toString().size()>4096)return false;}
                else if(booleanArrays.contains(key)){for(const auto& v:a)if(!v.isBool())return false;}
                else if(numericArrays.contains(key)){for(const auto& v:a)if(!v.isDouble()||!std::isfinite(v.toDouble())||std::abs(v.toDouble())>1000000)return false;}
                else return false;
            }else if(strings.contains(key)){if(!value.isString()||value.toString().size()>4096||value.toString().contains(QChar::Null))return false;}
            else if(booleans.contains(key)){if(!value.isBool())return false;}
            else if(numbers.contains(key)){if(!value.isDouble()||!std::isfinite(value.toDouble())||std::abs(value.toDouble())>1000000)return false;}
            else return false;
        }return true;
    };
    if(!check(settings)){error="Malformed mixer settings: invalid field, type or array size";return false;}return true;
}
bool completeSettings(const QJsonObject& settings,const QJsonObject& schema){
    for(auto it=schema.begin();it!=schema.end();++it){
        if(!settings.contains(it.key()))return false;
        if(it.value().isObject()&&!completeSettings(settings.value(it.key()).toObject(),it.value().toObject()))return false;
        if(it.key()=="hardware"){
            const auto actual=settings.value(it.key()).toArray(),expected=it.value().toArray();
            if(actual.size()!=expected.size())return false;
            for(int i=0;i<actual.size();++i)if(!completeSettings(actual[i].toObject(),expected[i].toObject()))return false;
        }
    }return true;
}
bool rackValid(const QJsonObject& rack,QString& error){
    if(rack.size()!=1||!rack.value("plugins").isArray()||rack.value("plugins").toArray().size()>32){error="Invalid plugin rack (maximum 32 plugins per input)";return false;}
    static const QRegularExpression encoding("^([0-9]{1,8})\\.([.A-Za-z0-9+]*)$");
    for(const auto& entry:rack.value("plugins").toArray()){
        if(!entry.isObject()){error="Invalid plugin entry";return false;}
        const auto p=entry.toObject();ProfilePlugin info;
        if(p.size()!=3||!p.value("description").isString()||!p.value("state").isString()||!p.value("bypass").isBool()||!profilePlugin(p.value("description").toString(),info,error)){if(error.isEmpty())error="Invalid plugin description/state/bypass";return false;}
        const auto state=p.value("state").toString();if(state.isEmpty())continue;
        const auto match=encoding.match(state);const auto bytes=match.captured(1).toLongLong();
        if(!match.hasMatch()||bytes>4*1024*1024||match.capturedLength(2)!=(bytes*8+5)/6){error="Invalid or oversized saved plugin state (maximum 4 MiB per plugin)";return false;}
    }return true;
}
}
bool profilePlugin(const QString& xml,ProfilePlugin& plugin,QString& error){
    if(xml.size()>16384){error="Oversized plugin description";return false;}
    QXmlStreamReader reader(xml);int elements=0;
    while(!reader.atEnd()){
        reader.readNext();if(reader.isDTD()||reader.isEntityReference()){error="Plugin XML entities/DTDs are not supported";return false;}
        if(reader.isStartElement()){
            if(++elements!=1||reader.name()!=QStringLiteral("PLUGIN")){error="Invalid plugin XML";return false;}
            const auto a=reader.attributes();plugin={a.value("name").toString(),a.value("format").toString(),a.value("file").toString()};
        }
    }
    if(reader.hasError()||elements!=1||plugin.name.isEmpty()||(plugin.format!="VST3"&&plugin.format!="LADSPA")||!QDir::isAbsolutePath(plugin.file)||plugin.file.contains(QChar::Null)||plugin.file.size()>4096){error="Invalid or unsupported plugin description";return false;}
    return true;
}
QString relocateProfilePlugin(const QString& xml,const QString& file){
    QXmlStreamReader reader(xml);QString result;QXmlStreamWriter writer(&result);
    while(!reader.atEnd()){reader.readNext();if(reader.isStartElement()){
        writer.writeStartElement(reader.name().toString());for(const auto& a:reader.attributes())writer.writeAttribute(a.name().toString(),a.name()==QStringLiteral("file")?file:a.value().toString());
    }else if(reader.isEndElement())writer.writeEndElement();}return result;
}
QByteArray encodeProfile(const ProfileBundle& profile){
    QJsonArray racks;for(const auto& rack:profile.racks)racks.append(rack);
    return QJsonDocument(QJsonObject{{"format","voicemixer-profile"},{"bundleVersion",1},{"settings",profile.settings},{"racks",racks}}).toJson();
}
bool parseProfile(const QByteArray& data,ProfileBundle& result,QString& error){
    error.clear();if(data.size()>profileSizeLimit){error="Profile exceeds the 64 MiB limit";return false;}
    QJsonParseError parse;const auto document=QJsonDocument::fromJson(data,&parse);
    if(parse.error!=QJsonParseError::NoError||!document.isObject()){error="Invalid JSON profile";return false;}
    const auto root=document.object();ProfileBundle candidate;
    if(root.contains("format")||root.contains("bundleVersion")){
        if(root.value("format")!="voicemixer-profile"||root.value("bundleVersion").toDouble()!=1||root.size()!=4||!root.value("settings").isObject()||!root.value("racks").isArray()||root.value("racks").toArray().size()!=8){error="Unsupported or incomplete profile bundle";return false;}
        candidate.settings=root.value("settings").toObject();const auto racks=root.value("racks").toArray();
        for(int i=0;i<8;++i){if(!racks[i].isObject()||!rackValid(racks[i].toObject(),error))return false;candidate.racks[i]=racks[i].toObject();}
        if(!candidate.settings.contains("hardware")||!candidate.settings.contains("routing")||!candidate.settings.contains("pluginRacks")){error="Incomplete profile settings";return false;}
    }else{candidate.legacy=true;candidate.settings=root;}
    if(!settingsValid(candidate.settings,error))return false;
    if(!candidate.legacy&&!completeSettings(candidate.settings,Settings{}.json())){error="Incomplete profile settings";return false;}
    result=candidate;return true;
}
bool readProfile(const QString& path,ProfileBundle& bundle,QString& error){
    QFile file(path);if(!file.open(QIODevice::ReadOnly)){error="Cannot read profile: "+file.errorString();return false;}
    if(file.size()>profileSizeLimit){error="Profile exceeds the 64 MiB limit";return false;}
    return parseProfile(file.read(profileSizeLimit+1),bundle,error);
}
bool writeProfileFile(const QString& path,const QByteArray& data,QString& error){
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)){error=file.errorString();return false;}
    file.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
    if(file.write(data)!=data.size()||!file.commit()){error=file.errorString();return false;}return true;
}
bool readProfileRacks(const QString& root,ProfileBundle& profile,QString& error){
    for(int i=0;i<8;++i){
        const auto path=root+QString("/plugin-racks/input%1/rack.json").arg(i);QFile file(path);
        if(!file.exists()){profile.racks[i]={{"plugins",QJsonArray{}}};continue;}
        if(!file.open(QIODevice::ReadOnly)||file.size()>profileSizeLimit){error="Cannot read plugin rack "+QString::number(i+1);return false;}
        QJsonParseError parse;const auto doc=QJsonDocument::fromJson(file.read(profileSizeLimit+1),&parse);
        if(parse.error!=QJsonParseError::NoError||!doc.isObject()||!rackValid(doc.object(),error)){error="Rack "+QString::number(i+1)+": "+error;return false;}profile.racks[i]=doc.object();
    }return true;
}
bool installProfile(const QString& root,const ProfileBundle& profile,QString& backup,QString& error){
    ProfileBundle validated;if(!parseProfile(profile.legacy?QJsonDocument(profile.settings).toJson():encodeProfile(profile),validated,error))return false;
    QMap<QString,QByteArray> writes;writes.insert("settings.json",QJsonDocument(profile.settings).toJson());
    if(!profile.legacy)for(int i=0;i<8;++i)writes.insert(QString("plugin-racks/input%1/rack.json").arg(i),QJsonDocument(profile.racks[i]).toJson());
    backup=root+"/profile-backups/"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-")+QUuid::createUuid().toString(QUuid::Id128);
    if(!QDir().mkpath(backup)){error="Cannot create rollback backup";return false;}QFile::setPermissions(backup,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    QMap<QString,QByteArray> before;QStringList absent;
    for(auto it=writes.begin();it!=writes.end();++it){
        const auto path=root+"/"+it.key();QString ancestor=path;
        while(ancestor.startsWith(root+"/")){if(QFileInfo(ancestor).isSymLink()){error="Profile destination contains a symbolic link: "+it.key();return false;}ancestor=QFileInfo(ancestor).path();}
        QFile file(path);if(!file.exists()){absent.append(it.key());continue;}
        if(!file.open(QIODevice::ReadOnly)||file.size()>profileSizeLimit){error="Cannot back up "+it.key();return false;}
        before.insert(it.key(),file.readAll());QDir().mkpath(QFileInfo(backup+"/"+it.key()).path());
        if(!writeProfileFile(backup+"/"+it.key(),before.value(it.key()),error))return false;
    }
    QStringList completed;
    for(auto it=writes.begin();it!=writes.end();++it){
        const auto path=root+"/"+it.key();
        if(!QDir().mkpath(QFileInfo(path).path())||!writeProfileFile(path,it.value(),error)){
            const auto original=error;QStringList failed;
            for(const auto& name:completed){QString rollbackError;if(absent.contains(name)){if(!QFile::remove(root+"/"+name))failed.append(name);}else if(!writeProfileFile(root+"/"+name,before.value(name),rollbackError))failed.append(name);}
            error="Restore failed: "+original+". Previous files "+(failed.isEmpty()?"restored.":"need recovery from "+backup+": "+failed.join(", "));return false;
        }completed.append(it.key());
    }return true;
}
