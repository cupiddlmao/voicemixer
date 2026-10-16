#include "console.h"
#include "routing.h"
#include <QtWidgets/QDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QTimeEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QMessageBox>
void Console::recorderOptions(){
    auto& recorder=*e.recorder;if(recorder.recording||recorder.finalizing){QMessageBox::information(this,"Recorder options","Stop recording before changing file and source options.");return;}
    auto& s=e.settings.tape;QDialog dialog(this);dialog.setWindowTitle("Tape Recorder Options · I/O arming");auto* form=new QFormLayout(&dialog);
    QRadioButton pre("Pre-fader inputs (original signals, mixed to stereo)"),post("Post-fader output (one bus)");pre.setChecked(!s.busMode);post.setChecked(s.busMode);form->addRow(&pre);
    QWidget arms;QGridLayout grid(&arms);std::array<QCheckBox*,8> armed{};
    for(int i=0;i<8;++i){armed[i]=new QCheckBox(i<5?QString("Hardware %1").arg(i+1):QStringList{"System","Music","AUX"}[i-5],&arms);armed[i]->setChecked(s.armed[i]);grid.addWidget(armed[i],i/4,i%4);}form->addRow(&arms);form->addRow(&post);
    QComboBox bus;for(int i=0;i<8;++i)bus.addItem(i<5?QString("A%1").arg(i+1):QString("B%1").arg(i-4));bus.setCurrentIndex(s.bus);form->addRow("Record bus",&bus);
    QLineEdit directory(s.directory),prefix(s.prefix);QWidget folder;QHBoxLayout folderLayout(&folder);folderLayout.setContentsMargins(0,0,0,0);QPushButton browse("Browse…");folderLayout.addWidget(&directory);folderLayout.addWidget(&browse);
    connect(&browse,&QPushButton::clicked,&dialog,[&]{auto path=QFileDialog::getExistingDirectory(&dialog,"Recording folder",directory.text());if(!path.isEmpty())directory.setText(path);});form->addRow("Target directory",&folder);form->addRow("Filename prefix",&prefix);
    QComboBox format,rate,bits,channels;format.addItems({"WAV","BWF","AIFF","MP3"});format.setCurrentText(s.format);for(int v:{8000,11025,16000,22050,24000,32000,44100,48000,88200,96000,176400,192000})rate.addItem(QString::number(v));rate.setCurrentText(QString::number(s.rate));for(int v:{8,16,24,32})bits.addItem(v==32?"32 float":QString::number(v),v);bits.setCurrentIndex(bits.findData(s.bits));for(int i=1;i<=8;++i)channels.addItem(QString::number(i));channels.setCurrentIndex(s.channels-1);
    form->addRow("File type",&format);form->addRow("Sample rate (Hz)",&rate);form->addRow("Bit resolution",&bits);form->addRow("Channels (3–8 require a Composite bus)",&channels);
    QSpinBox bitrate,buffer;bitrate.setRange(32,320);bitrate.setValue(s.kbps);bitrate.setSuffix(" kbps");buffer.setRange(10,60);buffer.setValue(s.bufferSeconds);buffer.setSuffix(" seconds");form->addRow("MP3 bitrate",&bitrate);form->addRow("Rolling buffer allocation",&buffer);
    QDoubleSpinBox preRoll;preRoll.setRange(0,s.bufferSeconds);preRoll.setValue(s.preSeconds);preRoll.setSuffix(" seconds");connect(&buffer,&QSpinBox::valueChanged,&preRoll,[&](int v){preRoll.setMaximum(v);});form->addRow("Pre-record time to save",&preRoll);
    QCheckBox multi("Also generate one mono file per channel"),autoplay("Play on load"),loop("Loop playback");multi.setChecked(s.multitrack);autoplay.setChecked(s.playOnLoad);loop.setChecked(s.loop);form->addRow(&multi);form->addRow(&autoplay);form->addRow(&loop);
    QDoubleSpinBox gain;gain.setRange(-60,12);gain.setDecimals(1);gain.setValue(s.gain);gain.setSuffix(" dB");form->addRow("Playback gain",&gain);
    QTimeEdit stopAfter;stopAfter.setDisplayFormat("HH:mm:ss");stopAfter.setTime(QTime(0,0).addSecs(int(s.stopAfter)));form->addRow("Stop record after (00:00:00 = off)",&stopAfter);
    auto enable=[&]{arms.setEnabled(pre.isChecked());bus.setEnabled(post.isChecked());channels.setEnabled(post.isChecked()&&format.currentText()!="MP3");bits.setEnabled(format.currentText()!="MP3");bitrate.setEnabled(format.currentText()=="MP3");};
    connect(&post,&QRadioButton::toggled,&dialog,[&]{enable();});connect(&format,&QComboBox::currentTextChanged,&dialog,[&]{enable();});enable();
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);form->addRow(&buttons);
    connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(&buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        if(format.currentText()=="MP3"&&rate.currentText().toInt()>48000){QMessageBox::information(&dialog,"MP3 sample rate","Choose a sample rate of 48 kHz or lower for MP3.");return;}
        const int count=post.isChecked()&&format.currentText()!="MP3"?channels.currentIndex()+1:2;
        if(count>2&&!e.settings.routing.busComposite[bus.currentIndex()]){QMessageBox::information(&dialog,"Composite bus required","Set this bus to Composite in its master mode menu before recording more than two channels.");return;}
        s.busMode=post.isChecked();s.bus=bus.currentIndex();s.channels=count;s.directory=directory.text();s.prefix=prefix.text();s.format=format.currentText();s.rate=rate.currentText().toInt();s.bits=bits.currentData().toInt();s.kbps=bitrate.value();s.bufferSeconds=buffer.value();s.preSeconds=preRoll.value();s.multitrack=multi.isChecked();s.playOnLoad=autoplay.isChecked();s.loop=loop.isChecked();s.gain=gain.value();s.stopAfter=QTime(0,0).secsTo(stopAfter.time());for(int i=0;i<8;++i)s.armed[i]=armed[i]->isChecked();recorder.configure();e.apply();dialog.accept();
    });dialog.exec();
}
void Console::busOptions(int index){
    if(e.recorder->recording||e.recorder->finalizing){QMessageBox::information(this,"Bus mode","Stop recording before changing bus topology.");return;}
    auto& state=e.settings.routing;QDialog dialog(this);dialog.setWindowTitle(QString("Bus %1 · Composite patch").arg(index<5?QString("A%1").arg(index+1):QString("B%1").arg(index-4)));QFormLayout form(&dialog);
    QCheckBox composite("Composite: 8 independent pre-fader channels");composite.setChecked(state.busComposite[index]);form.addRow(&composite);
    QCheckBox post("Use post-fader input signals instead of pre-fader");post.setChecked(state.busCompositePost[index]);form.addRow(&post);
    std::array<QComboBox*,8> patches{};
    for(int c=0;c<8;++c){auto* box=patches[c]=new QComboBox(&dialog);for(int input=0;input<8;++input)for(int side=0;side<2;++side)box->addItem((input<5?QString("Hardware %1").arg(input+1):QStringList{"System","Music","AUX"}[input-5])+(side?" · R / ch2":" · L / ch1"));box->setCurrentIndex(state.compositePatch[index][c]);form.addRow(QString("Channel %1").arg(c+1),box);}
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);form.addRow(&buttons);connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()==QDialog::Accepted){bool changed=state.busComposite[index]!=composite.isChecked();state.busComposite[index]=composite.isChecked();changed=changed||state.busCompositePost[index]!=post.isChecked();state.busCompositePost[index]=post.isChecked();for(int c=0;c<8;++c){changed=changed||state.compositePatch[index][c]!=patches[c]->currentIndex();state.compositePatch[index][c]=patches[c]->currentIndex();}e.save();if(changed)e.start();}
}
void Console::fxOptions(int effect){
    auto& s=e.settings.routing;QDialog dialog(this);dialog.setWindowTitle(effect==0?"Special FX · Reverb":"Special FX · Delay");QFormLayout form(&dialog);
    QCheckBox enabled("Enabled");enabled.setChecked(effect==0?s.reverbEnabled:s.delayEnabled);form.addRow(&enabled);
    QDoubleSpinBox first,second;first.setDecimals(2);second.setDecimals(2);
    if(effect==0){first.setRange(0,1);first.setValue(s.room);second.setRange(0,.98);second.setValue(s.damping);form.addRow("Room / decay",&first);form.addRow("Damping",&second);}
    else {first.setRange(1,2000);first.setValue(s.delayMs);first.setSuffix(" ms");second.setRange(0,.85);second.setValue(s.feedback);form.addRow("Delay time",&first);form.addRow("Feedback",&second);}
    std::array<QDoubleSpinBox*,8> returns{};for(int b=0;b<8;++b){auto* box=returns[b]=new QDoubleSpinBox(&dialog);box->setRange(0,100);box->setDecimals(0);box->setSuffix(" %");box->setValue(100*(effect==0?s.reverbReturn[b]:s.delayReturn[b]));form.addRow((b<5?QString("A%1").arg(b+1):QString("B%1").arg(b-4))+" return",box);}
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);form.addRow(&buttons);connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()==QDialog::Accepted){if(effect==0){s.reverbEnabled=enabled.isChecked();s.room=first.value();s.damping=second.value();}else{s.delayEnabled=enabled.isChecked();s.delayMs=first.value();s.feedback=second.value();}for(int b=0;b<8;++b)(effect==0?s.reverbReturn[b]:s.delayReturn[b])=returns[b]->value()/100;e.apply();}
}
