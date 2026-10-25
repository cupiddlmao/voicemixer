#include "update_checker.h"
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QVersionNumber>
ReleaseUpdate newestUpdate(const QByteArray& json,const QString& current,bool previews,QString* error){
    if(error)error->clear();
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(json,&parse);
    if(parse.error!=QJsonParseError::NoError||!doc.isArray()){if(error)*error="GitHub returned an invalid release list";return {};}
    ReleaseUpdate best;auto highest=QVersionNumber::fromString(current);
    const QRegularExpression tagPattern("^v?([0-9]+\\.[0-9]+\\.[0-9]+)$");
    for(const auto& value:doc.array()){
        const auto release=value.toObject();const bool preview=release.value("prerelease").toBool();
        if(release.value("draft").toBool()||(!previews&&preview))continue;
        const auto tag=release.value("tag_name").toString();const auto match=tagPattern.match(tag);if(!match.hasMatch())continue;
        const auto version=QVersionNumber::fromString(match.captured(1));if(version<=highest)continue;
        const QUrl url(release.value("html_url").toString());
        // Only offer our own release pages. Never execute assets or remote notes.
        if(url.scheme()!="https"||url.host()!="github.com"||!url.userInfo().isEmpty()||url.port()!=-1||url.hasQuery()||url.hasFragment()||url.path()!="/cupiddlmao/voicemixer/releases/tag/"+tag)continue;
        best={match.captured(1),url,preview};highest=version;
    }
    return best;
}
