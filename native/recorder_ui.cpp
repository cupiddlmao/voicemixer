#include "console.h"
#include "routing.h"
#include "ui_dialog.h"
#include <QtWidgets/QDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSpacerItem>
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
#include <array>

namespace {
// A form gets its breaks from uppercase headers with air above them, not from
// boxing every control.
void addSection(QFormLayout* form,const QString& name){
    form->addItem(new QSpacerItem(0,int(theme::unit),QSizePolicy::Fixed,QSizePolicy::Fixed));
    form->addRow(ui::section(name));
}

void alignForm(QFormLayout* form){
    form->setLabelAlignment(Qt::AlignLeft|Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft|Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
}
}

void Console::recorderOptions(){
    auto& recorder=*e.recorder;if(recorder.recording||recorder.finalizing){QMessageBox::information(this,"Recorder options","Stop recording before changing file and source options.");return;}
    auto& s=e.settings.tape;QDialog dialog(this);styleMixerDialog(dialog);dialog.setMinimumWidth(840);dialog.setWindowTitle("Tape Recorder Options · I/O arming");
    // Twenty-four rows do not belong in one column. Source and files sit
    // beside format and playback, so the dialog is a panel, not a scroll.
    QVBoxLayout shell(&dialog);ui::decorate(dialog);
    QWidget columns;QHBoxLayout columnLayout(&columns);columnLayout.setContentsMargins(0,0,0,0);columnLayout.setSpacing(32);
    QWidget left,right;QFormLayout leftForm(&left),rightForm(&right);
    alignForm(&leftForm);alignForm(&rightForm);
    leftForm.setContentsMargins(0,0,0,0);rightForm.setContentsMargins(0,0,0,0);
    // Floors, not guesses: the left column carries a path field, the right
    // carries suffixed numbers, and neither may be squeezed into an ellipsis.
    left.setMinimumWidth(430);right.setMinimumWidth(340);
    columnLayout.addWidget(&left,5);columnLayout.addWidget(&right,4);
    shell.addWidget(&columns,1);
    QRadioButton pre("Pre-fader inputs (original signals, mixed to stereo)"),post("Post-fader output (one bus)");pre.setChecked(!s.busMode);post.setChecked(s.busMode);
    QWidget arms;QGridLayout grid(&arms);grid.setContentsMargins(0,0,0,0);grid.setHorizontalSpacing(16);grid.setVerticalSpacing(6);std::array<QCheckBox*,8> armed{};
    for(int i=0;i<8;++i){armed[i]=new QCheckBox(i<5?QString("Hardware %1").arg(i+1):QStringList{"System","Music","AUX"}[i-5],&arms);armed[i]->setChecked(s.armed[i]);armed[i]->setFont(theme::font(theme::type::label));ui::setWarnTone(armed[i]);grid.addWidget(armed[i],i/2,i%2);}
    addSection(&leftForm,"Source");leftForm.addRow(&pre);leftForm.addRow(&post);
    addSection(&leftForm,"Arm inputs");leftForm.addRow(&arms);
    QComboBox bus;bus.setMaximumWidth(260);for(int i=0;i<8;++i)bus.addItem(i<5?QString("A%1").arg(i+1):QString("B%1").arg(i-4));bus.setCurrentIndex(s.bus);
    addSection(&leftForm,"Record bus");leftForm.addRow("Record bus",&bus);
    QWidget folder;QLineEdit directory(s.directory),prefix(s.prefix);directory.setMinimumWidth(220);directory.setCursorPosition(0);directory.setToolTip(s.directory);QHBoxLayout folderLayout(&folder);folderLayout.setContentsMargins(0,0,0,0);QPushButton browse("Browse…");folderLayout.addWidget(&directory);folderLayout.addWidget(&browse);
    connect(&browse,&QPushButton::clicked,&dialog,[&]{auto path=QFileDialog::getExistingDirectory(&dialog,"Recording folder",directory.text());if(!path.isEmpty())directory.setText(path);});
    addSection(&leftForm,"Files");leftForm.addRow("Target directory",&folder);leftForm.addRow("Filename prefix",&prefix);
    QComboBox format,rate,bits,channels;format.addItems({"WAV","BWF","AIFF","MP3"});format.setCurrentText(s.format);for(int v:{8000,11025,16000,22050,24000,32000,44100,48000,88200,96000,176400,192000})rate.addItem(QString::number(v));rate.setCurrentText(QString::number(s.rate));for(int v:{8,16,24,32})bits.addItem(v==32?"32 float":QString::number(v),v);bits.setCurrentIndex(bits.findData(s.bits));for(int i=1;i<=8;++i)channels.addItem(QString::number(i));channels.setCurrentIndex(s.channels-1);
    channels.setFont(ui::mono(theme::type::label));rate.setFont(ui::mono(theme::type::label));bits.setFont(ui::mono(theme::type::label));
    // The constraint reads as a note under its own field. Beside it, it
    // competes for the column and squeezes the number.
    auto* channelsHint=new QLabel("(3–8 require a Composite bus)");
    channelsHint->setFont(theme::font(theme::type::value));ui::setWarnTone(channelsHint);
    addSection(&rightForm,"Capture format");rightForm.addRow("File type",&format);rightForm.addRow("Sample rate (Hz)",&rate);rightForm.addRow("Bit resolution",&bits);rightForm.addRow("Channels",&channels);rightForm.addRow(channelsHint);
    QSpinBox bitrate,buffer;bitrate.setRange(32,320);bitrate.setValue(s.kbps);bitrate.setSuffix(" kbps");buffer.setRange(10,60);buffer.setValue(s.bufferSeconds);buffer.setSuffix(" seconds");
    bitrate.setFont(ui::mono(theme::type::label));buffer.setFont(ui::mono(theme::type::label));
    QDoubleSpinBox preRoll;preRoll.setRange(0,s.bufferSeconds);preRoll.setValue(s.preSeconds);preRoll.setSuffix(" seconds");preRoll.setFont(ui::mono(theme::type::label));connect(&buffer,&QSpinBox::valueChanged,&preRoll,[&](int v){preRoll.setMaximum(v);});
    QTimeEdit stopAfter;stopAfter.setDisplayFormat("HH:mm:ss");stopAfter.setTime(QTime(0,0).addSecs(int(s.stopAfter)));stopAfter.setFont(ui::mono(theme::type::label));
    addSection(&rightForm,"Buffer and limits");rightForm.addRow("MP3 bitrate",&bitrate);rightForm.addRow("Rolling buffer allocation",&buffer);rightForm.addRow("Pre-record time to save",&preRoll);rightForm.addRow("Stop record after (00:00:00 = off)",&stopAfter);
    QCheckBox multi("Also generate one mono file per channel"),autoplay("Play on load"),loop("Loop playback");multi.setChecked(s.multitrack);autoplay.setChecked(s.playOnLoad);loop.setChecked(s.loop);
    for(auto* box:std::array<QCheckBox*,3>{&multi,&autoplay,&loop})box->setFont(theme::font(theme::type::label));
    QDoubleSpinBox gain;gain.setRange(-60,12);gain.setDecimals(1);gain.setValue(s.gain);gain.setSuffix(" dB");gain.setFont(ui::mono(theme::type::label));
    addSection(&rightForm,"Playback");rightForm.addRow(&multi);rightForm.addRow(&autoplay);rightForm.addRow(&loop);rightForm.addRow("Playback gain",&gain);
    auto enable=[&]{arms.setEnabled(pre.isChecked());bus.setEnabled(post.isChecked());channels.setEnabled(post.isChecked()&&format.currentText()!="MP3");bits.setEnabled(format.currentText()!="MP3");bitrate.setEnabled(format.currentText()=="MP3");};
    connect(&post,&QRadioButton::toggled,&dialog,[&]{enable();});connect(&format,&QComboBox::currentTextChanged,&dialog,[&]{enable();});enable();
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    ui::primary(buttons.button(QDialogButtonBox::Ok));
    QHBoxLayout footer;footer.addStretch();footer.addWidget(&buttons);
    shell.addLayout(&footer);
    connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(&buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        if(format.currentText()=="MP3"&&rate.currentText().toInt()>48000){QMessageBox::information(&dialog,"MP3 sample rate","Choose a sample rate of 48 kHz or lower for MP3.");return;}
        const int count=post.isChecked()&&format.currentText()!="MP3"?channels.currentIndex()+1:2;
        if(count>2&&!e.settings.routing.busComposite[bus.currentIndex()]){QMessageBox::information(&dialog,"Composite bus required","Set this bus to Composite in its master mode menu before recording more than two channels.");return;}
        s.busMode=post.isChecked();s.bus=bus.currentIndex();s.channels=count;s.directory=directory.text();s.prefix=prefix.text();s.format=format.currentText();s.rate=rate.currentText().toInt();s.bits=bits.currentData().toInt();s.kbps=bitrate.value();s.bufferSeconds=buffer.value();s.preSeconds=preRoll.value();s.multitrack=multi.isChecked();s.playOnLoad=autoplay.isChecked();s.loop=loop.isChecked();s.gain=gain.value();s.stopAfter=QTime(0,0).secsTo(stopAfter.time());for(int i=0;i<8;++i)s.armed[i]=armed[i]->isChecked();recorder.configure();e.apply();dialog.accept();
    });dialog.exec();
}
void Console::busOptions(int index){
    if(e.recorder->recording||e.recorder->finalizing){QMessageBox::information(this,"Bus mode","Stop recording before changing bus topology.");return;}
    auto& state=e.settings.routing;QDialog dialog(this);styleMixerDialog(dialog);dialog.setMinimumWidth(440);dialog.setWindowTitle(QString("Bus %1 · Composite patch").arg(index<5?QString("A%1").arg(index+1):QString("B%1").arg(index-4)));QFormLayout form(&dialog);
    alignForm(&form);ui::decorate(dialog);
    QCheckBox composite("Composite: 8 independent pre-fader channels");composite.setChecked(state.busComposite[index]);composite.setFont(theme::font(theme::type::label));
    QCheckBox post("Use post-fader input signals instead of pre-fader");post.setChecked(state.busCompositePost[index]);post.setFont(theme::font(theme::type::label));
    addSection(&form,"Topology");form.addRow(&composite);form.addRow(&post);
    std::array<QComboBox*,8> patches{};
    addSection(&form,"Channel patch");
    for(int c=0;c<8;++c){auto* box=patches[c]=new QComboBox(&dialog);for(int input=0;input<8;++input)for(int side=0;side<2;++side)box->addItem((input<5?QString("Hardware %1").arg(input+1):QStringList{"System","Music","AUX"}[input-5])+(side?" · R / ch2":" · L / ch1"));box->setCurrentIndex(state.compositePatch[index][c]);box->setMinimumWidth(260);form.addRow(QString("Channel %1").arg(c+1),box);}
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    ui::primary(buttons.button(QDialogButtonBox::Ok));
    form.addRow(&buttons);connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()==QDialog::Accepted){bool changed=state.busComposite[index]!=composite.isChecked();state.busComposite[index]=composite.isChecked();changed=changed||state.busCompositePost[index]!=post.isChecked();state.busCompositePost[index]=post.isChecked();for(int c=0;c<8;++c){changed=changed||state.compositePatch[index][c]!=patches[c]->currentIndex();state.compositePatch[index][c]=patches[c]->currentIndex();}e.save();if(changed)e.start();}
}
void Console::fxOptions(int effect){
    auto& s=e.settings.routing;QDialog dialog(this);styleMixerDialog(dialog);dialog.setMinimumWidth(320);dialog.setWindowTitle(effect==0?"Special FX · Reverb":"Special FX · Delay");QFormLayout form(&dialog);
    alignForm(&form);ui::decorate(dialog);
    QCheckBox enabled("Enabled");enabled.setChecked(effect==0?s.reverbEnabled:s.delayEnabled);enabled.setFont(theme::font(theme::type::label));
    addSection(&form,"Send");form.addRow(&enabled);
    QDoubleSpinBox first,second;first.setDecimals(2);second.setDecimals(2);
    first.setFont(ui::mono(theme::type::label));second.setFont(ui::mono(theme::type::label));
    addSection(&form,effect==0?"Room":"Time");
    if(effect==0){first.setRange(0,1);first.setValue(s.room);second.setRange(0,.98);second.setValue(s.damping);form.addRow("Room / decay",&first);form.addRow("Damping",&second);}
    else {first.setRange(1,2000);first.setValue(s.delayMs);first.setSuffix(" ms");second.setRange(0,.85);second.setValue(s.feedback);form.addRow("Delay time",&first);form.addRow("Feedback",&second);}
    std::array<QDoubleSpinBox*,8> returns{};
    addSection(&form,"Bus returns");
    for(int b=0;b<8;++b){auto* box=returns[b]=new QDoubleSpinBox(&dialog);box->setRange(0,100);box->setDecimals(0);box->setSuffix(" %");box->setValue(100*(effect==0?s.reverbReturn[b]:s.delayReturn[b]));box->setFont(ui::mono(theme::type::label));form.addRow((b<5?QString("A%1").arg(b+1):QString("B%1").arg(b-4))+" return",box);}
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    ui::primary(buttons.button(QDialogButtonBox::Ok));
    form.addRow(&buttons);connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()==QDialog::Accepted){if(effect==0){s.reverbEnabled=enabled.isChecked();s.room=first.value();s.damping=second.value();}else{s.delayEnabled=enabled.isChecked();s.delayMs=first.value();s.feedback=second.value();}for(int b=0;b<8;++b)(effect==0?s.reverbReturn[b]:s.delayReturn[b])=returns[b]->value()/100;e.apply();}
}
