#include "profile_ui.h"
#include "plugin_racks.h"
#include "routing.h"
#include "paths.h"
#include "console.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFormLayout>
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
    QVBoxLayout layout(&dialog);QLabel explanation("Review devices before restoring. Plugin chains contain saved controls, not plugin software. Missing plugins are retained in the saved chain and the affected rack is disabled until resolved.");explanation.setWordWrap(true);layout.addWidget(&explanation);
    QScrollArea scroll;scroll.setWidgetResizable(true);QWidget content;QFormLayout form(&content);layout.addWidget(&scroll);scroll.setWidget(&content);
    std::array<QComboBox*,10> mappings{};QStringList notices;std::array<bool,8> missing{};
    for(int row=0;row<10;++row){
        const bool input=row<5;const int index=input?row:row-5;
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
        form.addRow(input?stripName(index):QString("Output A%1").arg(index+1),box);
    }
    if(!profile.legacy)for(int i=0;i<8;++i){
        QJsonArray plugins=profile.racks[i].value("plugins").toArray();
        for(int p=0;p<plugins.size();++p){auto plugin=plugins[p].toObject();ProfilePlugin info;QString error;profilePlugin(plugin.value("description").toString(),info,error);const auto located=resolvePlugin(info);
            if(located.isEmpty()){missing[i]=true;notices<<QString("Missing plugin on %1: %2 (%3)").arg(stripName(i),info.name,info.file);}
            else if(located!=info.file){plugin.insert("description",relocateProfilePlugin(plugin.value("description").toString(),located));plugins[p]=plugin;notices<<"Located plugin: "+info.name+" at "+located;}
        }profile.racks[i].insert("plugins",plugins);
    }
    auto* noticesLabel=new QLabel(notices.isEmpty()?"All referenced devices and plugin files were found. Plugin load/compatibility is checked when its rack starts.":notices.join('\n'),&content);noticesLabel->setObjectName("profileMissingNotices");noticesLabel->setTextFormat(Qt::PlainText);noticesLabel->setWordWrap(true);form.addRow(noticesLabel);
    QCheckBox chains("Restore all plugin chains and saved states (replaces current input racks)");chains.setObjectName("profileRestorePlugins");chains.setChecked(!profile.legacy);chains.setEnabled(!profile.legacy);layout.addWidget(&chains);
    QLabel rollback("A local rollback backup is created before applying the restore. Mixer audio may briefly reconnect.");rollback.setWordWrap(true);layout.addWidget(&rollback);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout.addWidget(&buttons);QObject::connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
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
