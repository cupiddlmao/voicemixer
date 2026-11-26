#include "update_checker.h"
#include "paths.h"
#include "ui_dialog.h"
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QVersionNumber>
#include <QtCore/QDateTime>
#include <QtCore/QTimer>
#include <QtCore/QDir>
#include <QtNetwork/QNetworkRequest>
#include <QtGui/QDesktopServices>
#include <QtWidgets/QDialog>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QDialogButtonBox>
#include "console.h"
#ifndef VOICEMIXER_VERSION
#define VOICEMIXER_VERSION "0.5.1"
#endif
UpdateChecker::UpdateChecker(QObject* parent,QNetworkAccessManager* transport):QObject(parent),network(transport?transport:new QNetworkAccessManager(this)),preferences(stateRoot()+"/update-preferences.ini",QSettings::IniFormat){
    QTimer::singleShot(15000,this,[this]{
        const auto last=preferences.value("lastCheck").toDateTime();
        if(preferences.value("startup",false).toBool()&&(!last.isValid()||last.secsTo(QDateTime::currentDateTimeUtc())>=86400))check();
    });
}
void UpdateChecker::check(){
    if(busy)return;
    busy=true;update={};response.clear();status="Checking GitHub release information…";if(changed)changed();
    QNetworkRequest request(QUrl("https://api.github.com/repos/cupiddlmao/voicemixer/releases?per_page=10"));
    request.setRawHeader("Accept","application/vnd.github+json");request.setRawHeader("User-Agent","Voicemixer/" VOICEMIXER_VERSION);
    request.setRawHeader("X-GitHub-Api-Version","2022-11-28");request.setTransferTimeout(12000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    reply=network->get(request);reply->setReadBufferSize(32768);auto* current=reply;
    // A wall-clock deadline also covers a server that trickles bytes forever.
    QTimer::singleShot(15000,current,[current]{if(!current->isFinished()){current->setProperty("deadline",true);current->abort();}});
    connect(current,&QNetworkReply::readyRead,this,[this,current]{
        constexpr int maxResponse=256*1024;response+=current->read(maxResponse+1-response.size());
        if(response.size()>maxResponse){current->setProperty("oversized",true);current->abort();}
    });
    connect(current,&QNetworkReply::finished,this,[this,current]{
        busy=false;reply=nullptr;const int code=current->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(current->property("oversized").toBool())status="Release information exceeded the size limit. Try the releases page.";
        else if(current->property("deadline").toBool()||current->error()==QNetworkReply::TimeoutError)status="Update check timed out. You can try again later.";
        else if(code==403||code==429)status="GitHub's request limit was reached. Try again later.";
        else if(current->error()!=QNetworkReply::NoError||code!=200)status="Could not check updates. Check your connection and try again.";
        else {
            QString error;update=newestUpdate(response,VOICEMIXER_VERSION,preferences.value("previews",true).toBool(),&error);
            if(!error.isEmpty())status=error;
            else {preferences.setValue("lastCheck",QDateTime::currentDateTimeUtc());status=update.version.isEmpty()?"No newer release found in the selected channel.":"Voicemixer "+update.version+(update.preview?" preview":"")+" is available.";}
        }
        response.clear();current->deleteLater();if(changed)changed();
    });
}
void UpdateChecker::showDialog(QWidget* parent){
    QDialog dialog(parent);dialog.setWindowTitle("Voicemixer updates");dialog.resize(520,280);styleMixerDialog(dialog);
    auto* layout=new QVBoxLayout(&dialog);ui::decorate(dialog);
    // The installed version, marked as the thing that is actually running.
    auto* versionRow=new QHBoxLayout;versionRow->setSpacing(8);
    auto* version=new QLabel("Installed version: " VOICEMIXER_VERSION);layout->addLayout(versionRow);
    version->setFont(ui::mono(theme::type::label));
    version->setStyleSheet("color: #e8eff3; background: transparent;");
    versionRow->addWidget(version);versionRow->addStretch();
    versionRow->addWidget(ui::pill("CURRENT",ui::Tone::Active));
    auto* description=ui::note("Checks fetch a small release list from GitHub. Downloads and installation are manual. Your mixer settings stay in your profile folder.");layout->addWidget(description);
    auto* startup=new QCheckBox("Check at startup (at most once per day)");startup->setChecked(preferences.value("startup",false).toBool());startup->setFont(theme::font(theme::type::label));layout->addWidget(startup);
    auto* previews=new QCheckBox("Include preview releases");previews->setChecked(preferences.value("previews",true).toBool());previews->setFont(theme::font(theme::type::label));layout->addWidget(previews);
    auto* label=new QLabel;label->setTextFormat(Qt::PlainText);label->setWordWrap(true);label->setFont(ui::mono(theme::type::value));layout->addWidget(label);
    // One primary action; the release page sits beside it at its own width
    // instead of stretching into a second full-size bar.
    auto* actions=new QHBoxLayout;actions->setSpacing(12);
    auto* checkButton=ui::primary(new QPushButton("Check now"));
    auto* open=new QPushButton("Open release page in browser");
    actions->addWidget(checkButton);actions->addWidget(open);actions->addStretch();
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close);
    actions->addWidget(buttons);
    layout->addLayout(actions);
    const auto refresh=[&]{label->setText(status);checkButton->setEnabled(!busy);previews->setEnabled(!busy);open->setEnabled(!update.url.isEmpty());};
    connect(startup,&QCheckBox::toggled,&dialog,[this](bool on){QDir().mkpath(stateRoot());preferences.setValue("startup",on);});
    connect(previews,&QCheckBox::toggled,&dialog,[this,refresh](bool on){QDir().mkpath(stateRoot());preferences.setValue("previews",on);update={};status="Channel changed. Check now to refresh.";refresh();});
    connect(checkButton,&QPushButton::clicked,&dialog,[this]{check();});
    connect(open,&QPushButton::clicked,&dialog,[this]{if(!update.url.isEmpty())QDesktopServices::openUrl(update.url);});
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    // Poll UI state only while this dialog exists; network work stays asynchronous.
    QTimer timer;timer.setInterval(150);connect(&timer,&QTimer::timeout,&dialog,refresh);timer.start();refresh();dialog.exec();
}
