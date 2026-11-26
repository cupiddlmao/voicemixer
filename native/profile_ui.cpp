#include "profile_ui.h"
#include "plugin_racks.h"
#include "routing.h"
#include "paths.h"
#include "console.h"
#include "ui_dialog.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QScrollArea>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtWidgets/QVBoxLayout>
#include <QtCore/QScopedValueRollback>
#include <array>

namespace {
QString stripName(int i){return i<5?QString("Hardware input %1").arg(i+1):QStringList{"System","Music","AUX"}[i-5];}
QString resolvePlugin(const ProfilePlugin& plugin){
    if(QFileInfo::exists(plugin.file))return plugin.file;
    const auto base=QFileInfo(plugin.file).fileName();
    const QStringList directories=plugin.format=="VST3"?QStringList{QDir::homePath()+"/.vst3",QDir::homePath()+"/.vst3/yabridge","/usr/lib/vst3","/usr/local/lib/vst3"}:QStringList{QDir::homePath()+"/.ladspa","/usr/lib/ladspa","/usr/lib64/ladspa","/usr/local/lib/ladspa"};
    for(const auto& dir:directories)if(QFileInfo::exists(dir+"/"+base))return dir+"/"+base;
    return {};
}
void reportProfile(Engine& engine,const QString& title,const QString& text){engine.message=text;if(engine.changed)engine.changed();QMessageBox::warning(QApplication::activeWindow(),title,text);}
}
bool reviewProfile(ProfileBundle& profile,const Settings& current,const QList<Device>& devices,QWidget* parent){
    Settings proposed=profile.legacy?current:Settings{};proposed.load(profile.settings);
    QDialog dialog(parent);dialog.setObjectName("profileRestoreReview");dialog.setWindowTitle("Restore mixer profile");dialog.resize(680,650);styleMixerDialog(dialog);
    QVBoxLayout layout(&dialog);ui::decorate(dialog);

    // Title line: what this is, and which kind of file it came from.
    QHBoxLayout header;header.setSpacing(8);
    header.addWidget(ui::title("Restore mixer profile"));
    header.addStretch();
    header.addWidget(ui::pill(profile.legacy?"LEGACY PRESET":"COMPLETE BUNDLE",profile.legacy?ui::Tone::Warn:ui::Tone::Active));
    layout.addLayout(&header);
    // Filled in once the mappings below have been resolved.
    auto* summary=ui::body(QString());
    layout.addWidget(summary);

    // Old-to-new list: the device named in the file next to the device that
    // will actually be restored. Scannable in one column sweep.
    layout.addWidget(ui::section("Device mappings"));
    QScrollArea scroll;scroll.setWidgetResizable(true);scroll.setFrameShape(QFrame::NoFrame);
    QWidget content;QGridLayout grid(&content);
    grid.setContentsMargins(0,0,0,0);grid.setHorizontalSpacing(12);grid.setVerticalSpacing(6);
    layout.addWidget(&scroll,1);scroll.setWidget(&content);
    grid.addWidget(ui::caption("Device"),0,0);
    grid.addWidget(ui::caption("In this file"),0,1);
    grid.addWidget(ui::caption("Restore as"),0,2);
    grid.setColumnStretch(2,1);
    std::array<QComboBox*,10> mappings{};QStringList notices;std::array<bool,8> missing{};
    int changed=0,gridRow=0;
    // The caption band gets a hairline and each group gets air above it, so
    // two rows of uppercase never touch and read as one noisy band.
    grid.addWidget(ui::rule(&content),++gridRow,0,1,3);
    const auto groupRow=[&](const QString& name){
        ++gridRow;grid.setRowMinimumHeight(gridRow,int(theme::unit));
        ++gridRow;grid.addWidget(ui::section(name),gridRow,0,1,3);
    };
    for(int row=0;row<10;++row){
        const bool input=row<5;const int index=input?row:row-5;
        if(input&&row==0)groupRow("Hardware inputs");
        if(!input&&row==5)groupRow("Outputs");
        const QString saved=input?proposed.hardware(index).mic:proposed.routing.outputs[index];
        const QString existing=input?current.hardware(index).mic:current.routing.outputs[index];
        auto* box=mappings[row]=new QComboBox(&content);box->setObjectName(QString("profileDevice%1").arg(row));box->addItem("Unassigned",QString{});
        for(const auto& device:devices)if(device.input==input)box->addItem(device.description,device.name);
        const bool found=saved.isEmpty()||box->findData(saved)>=0;
        if(!found)notices<<QString("Missing %1: %2").arg(input?"input device":"output device",saved);
        // Legacy presets retain the previous running-device convention by
        // default. Bundles use saved devices when available, otherwise offer
        // the corresponding currently selected device or Unassigned.
        const QString selected=profile.legacy?existing:(found?saved:existing);
        box->setCurrentIndex(std::max(0,box->findData(selected)));
        if(box->currentData().toString()!=existing)++changed;
        ++gridRow;
        grid.addWidget(ui::field(input?stripName(index):QString("Output A%1").arg(index+1),&content),gridRow,0);
        auto* from=ui::value(saved.isEmpty()?QString("unassigned"):saved,&content,found?ui::Tone::Muted:ui::Tone::Warn);
        from->setMinimumWidth(0);
        grid.addWidget(from,gridRow,1,Qt::AlignLeft|Qt::AlignVCenter);
        grid.addWidget(box,gridRow,2);
    }
    if(!profile.legacy)for(int i=0;i<8;++i){
        QJsonArray plugins=profile.racks[i].value("plugins").toArray();
        for(int p=0;p<plugins.size();++p){auto plugin=plugins[p].toObject();ProfilePlugin info;QString error;profilePlugin(plugin.value("description").toString(),info,error);const auto located=resolvePlugin(info);
            if(located.isEmpty()){missing[i]=true;notices<<QString("Missing plugin on %1: %2 (%3)").arg(stripName(i),info.name,info.file);}
            else if(located!=info.file){plugin.insert("description",relocateProfilePlugin(plugin.value("description").toString(),located));plugins[p]=plugin;notices<<"Located plugin: "+info.name+" at "+located;}
        }profile.racks[i].insert("plugins",plugins);
    }
    summary->setText(QString("%1 of %2 device mappings change. ").arg(changed).arg(10)
        +(profile.legacy?QString("This preset carries no plugin chains.")
                         :QString("Plugin chains and their saved states come with the bundle.")));

    // Notices: the one thing that can make a restore wrong, kept as plain
    // text because the file names in it are the point.
    auto* noticesHeader=new QHBoxLayout;noticesHeader->setSpacing(8);
    noticesHeader->addWidget(ui::section("Notices"));
    int problems=0;for(const auto& line:notices)if(line.startsWith("Missing "))++problems;
    noticesHeader->addWidget(ui::pill(problems?QString("%1 TO CHECK").arg(problems):QString("NONE"),
                                      problems?ui::Tone::Danger:ui::Tone::Active));
    noticesHeader->addStretch();
    layout.addLayout(noticesHeader);
    auto* noticesPanel=ui::panel();
    auto* noticesLabel=new QLabel(notices.isEmpty()?"All referenced devices and plugin files were found. Plugin load/compatibility is checked when its rack starts.":notices.join('\n'));
    noticesLabel->setObjectName("profileMissingNotices");noticesLabel->setTextFormat(Qt::PlainText);
    noticesLabel->setWordWrap(true);noticesLabel->setFont(ui::mono(theme::type::value));
    noticesLabel->setStyleSheet("color: #93a7b4; background: transparent;");
    ui::vboxIn(noticesPanel)->addWidget(noticesLabel);
    layout.addWidget(noticesPanel);

    QCheckBox chains("Restore all plugin chains and saved states (replaces current input racks)");chains.setObjectName("profileRestorePlugins");chains.setChecked(!profile.legacy);chains.setEnabled(!profile.legacy);
    chains.setFont(theme::font(theme::type::label));
    layout.addWidget(&chains);
    auto* rollback=ui::micro("A local rollback backup is created before applying the restore. Mixer audio may briefly reconnect.");
    layout.addWidget(rollback);
    auto* explanation=ui::micro("Review devices before restoring. Plugin chains contain saved controls, not plugin software. Missing plugins are retained in the saved chain and the affected rack is disabled until resolved.");
    layout.addWidget(explanation);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    ui::primary(buttons.button(QDialogButtonBox::Ok));
    layout.addWidget(&buttons);QObject::connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    // TEMPORARY CAPTURE SCAFFOLDING: photograph the review dialog, which has
    // no hook of its own. Render synchronously so the test's instant accept
    // cannot race it. DELETE before commit.
    if(const QString shots=qEnvironmentVariable("LINUX_MIXER_UI_TEST_OUTPUT");!shots.isEmpty()){
        static int take=0;
        QDir().mkpath(shots);dialog.show();dialog.grab().save(shots+QString("/profile-restore-%1.png").arg(++take));
    }
    const bool accepted=dialog.exec()==QDialog::Accepted;
    // content is a stack widget; detach it before the scroll area destroys it.
    scroll.takeWidget();if(!accepted)return false;
    for(int i=0;i<5;++i){
        const auto device=mappings[i]->currentData().toString();
        if(profile.legacy&&device==current.hardware(i).mic)proposed.hardware(i).channel=current.hardware(i).channel;
        else if(device!=proposed.hardware(i).mic)proposed.hardware(i).channel="MONO";
        proposed.hardware(i).mic=device;proposed.routing.outputs[i]=mappings[5+i]->currentData().toString();
    }
    proposed.sink=proposed.routing.outputs[0];proposed.engineEnabled=current.engineEnabled;
    if(!profile.legacy&&chains.isChecked()){for(int i=0;i<8;++i)if(missing[i])proposed.pluginRacks[i]=false;}
    else if(!profile.legacy){profile.legacy=true;proposed.pluginRacks=current.pluginRacks;}
    profile.settings=proposed.json();return true;
}
void Engine::exportPreset(const QString& path){
    if(path.isEmpty()||profileOperation)return;
    QScopedValueRollback<bool> guard(profileOperation,true);
    const auto destination=QFileInfo(path).exists()?QFileInfo(path).canonicalFilePath():QFileInfo(path).absoluteFilePath();
    const auto managed=QFileInfo(stateRoot()).canonicalFilePath();
    if(destination==managed+"/settings.json"||destination.startsWith(managed+"/plugin-racks/")){reportProfile(*this,"Profile backup failed","Choose a backup file outside the live settings and plugin-rack files");return;}
    QString error;ProfileBundle profile;
    if(!plugins->snapshot(error)){reportProfile(*this,"Profile backup failed",error);return;}
    profile.settings=settings.json();
    if(!readProfileRacks(stateRoot(),profile,error)){reportProfile(*this,"Profile backup failed",error);return;}
    const auto data=encodeProfile(profile);ProfileBundle validated;
    if(!parseProfile(data,validated,error)||!writeProfileFile(path,data,error)){reportProfile(*this,"Profile backup failed",error);return;}
    message="Complete profile saved (mixer settings, plugin chains and saved states)";notify();
}
void Engine::importPreset(const QString& path){
    if(path.isEmpty()||profileOperation)return;
    QScopedValueRollback<bool> guard(profileOperation,true);
    if(recorder->recording||recorder->finalizing){reportProfile(*this,"Profile restore", "Stop recording before restoring a profile");return;}
    ProfileBundle profile;QString error;
    if(!readProfile(path,profile,error)){reportProfile(*this,"Profile restore failed",error);return;}
    if(!reviewProfile(profile,settings,devices,QApplication::activeWindow())){message="Profile restore canceled";notify();return;}
    // Revalidate the remapped review result before touching live configuration.
    ProfileBundle validated;
    if(!parseProfile(profile.legacy?QJsonDocument(profile.settings).toJson():encodeProfile(profile),validated,error)){reportProfile(*this,"Profile restore failed",error);return;}
    if(recorder->recording||recorder->finalizing){reportProfile(*this,"Profile restore", "Stop recording before restoring a profile");return;}
    if(!plugins->snapshot(error)){reportProfile(*this,"Profile restore failed",error);return;}
    if(!plugins->pauseForProfile(error)){reportProfile(*this,"Profile restore failed",error);return;}
    const Settings previous=settings;const bool wasRunning=running;QString backup;
    if(!installProfile(stateRoot(),profile,backup,error)){plugins->resumeAfterProfile();reportProfile(*this,"Profile restore failed",error);return;}
    settings=Settings{};settings.load(profile.settings);settings.engineEnabled=previous.engineEnabled;
    recorder->configure();matrix->rulesChanged();
    bool reconnect=previous.routing.busComposite!=settings.routing.busComposite||previous.routing.busCompositePost!=settings.routing.busCompositePost||previous.routing.compositePatch!=settings.routing.compositePatch;
    for(int i=0;i<5;++i)reconnect=reconnect||previous.hardware(i).mic!=settings.hardware(i).mic||previous.hardware(i).channel!=settings.hardware(i).channel;
    plugins->resumeAfterProfile();if(wasRunning&&reconnect)start();apply();refresh();
    message="Profile restored. Rollback backup: "+backup;notify();
}
