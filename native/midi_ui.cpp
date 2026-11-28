#include "midi_ui.h"
#include "console.h"
#include "macros.h"
#include "control_commands.h"
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtCore/QMetaObject>
#include <algorithm>

namespace {
// -1 is "any channel" in the model; the editor spells it out rather than asking
// the user to remember a sentinel.
QString channelText(int channel) {return channel<0?QString("any"):QString::number(channel);}
}

void applyMidiBatch(Engine& engine,const QList<MidiAction>& actions) {
    if(actions.isEmpty())return;
    QStringList skipped;
    bool changed=false;
    for(const auto& action:actions) {
        if(action.command.verb==ControlCommand::Macro) {
            const QString name=action.command.name.trimmed();
            const Macro* found=nullptr;
            for(const auto& macro:engine.settings.macros.macros)
                if(QString::compare(macro.name,name,Qt::CaseInsensitive)==0){found=&macro;break;}
            // A binding can outlive the macro it names: the editor refuses that on
            // save, but a macro can still be deleted afterwards. Skip it and say so
            // on the status line; a dialog would be useless while a knob is moving.
            if(!found){
                skipped.append(QString("MIDI '%1': no macro named '%2'").arg(action.bindingId).arg(name));
                continue;
            }
            // Copy first: runMacro replaces engine.settings, so the list element
            // would dangle across the call.
            const Macro macro=*found;
            QString error;
            if(!runMacro(macro,engine.settings,&error)){
                skipped.append(QString("MIDI '%1': %2").arg(action.bindingId)
                    .arg(error.isEmpty()?QString("macro '%1' failed").arg(name):error));
                continue;
            }
            changed=true;
        } else {
            QString error;
            if(!applyControlCommand(engine.settings,action.command,error)){
                skipped.append(QString("MIDI '%1': %2").arg(action.bindingId)
                    .arg(error.isEmpty()?QString("action failed"):error));
                continue;
            }
            changed=true;
        }
    }
    if(!skipped.isEmpty())engine.message=skipped.join("; ");
    // One apply for the batch, never one per action. A batch that changed nothing
    // (only skipped macro actions) issues no graph update at all; the status line
    // is refreshed by hand instead.
    if(changed){engine.apply();engine.save();}
    else if(!skipped.isEmpty()&&engine.changed)engine.changed();
}

MidiEditor::MidiEditor(Engine& engine,MidiInput* input,QWidget* parent)
    :QDialog(parent),engine(engine),input(input),alive(std::make_shared<std::atomic<bool>>(true)) {
    setObjectName("midiEditor");setWindowTitle("MIDI bindings");resize(880,520);styleMixerDialog(*this);

    auto* outer=new QVBoxLayout(this);
    auto* body=new QHBoxLayout;outer->addLayout(body,1);
    auto* left=new QVBoxLayout;body->addLayout(left,1);
    left->addWidget(new QLabel("Bindings"));
    table=new QTableWidget(0,5);table->setObjectName("midiEditorTable");
    table->setHorizontalHeaderLabels({"Binding","Message","Channel","Number","Action"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(4,QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    left->addWidget(table,1);
    auto* rowButtons=new QHBoxLayout;
    auto* create=new QPushButton("New");create->setObjectName("midiEditorNew");
    auto* remove=new QPushButton("Delete");remove->setObjectName("midiEditorDelete");
    auto* learnButton=new QPushButton("Learn");learnButton->setObjectName("midiEditorLearn");
    rowButtons->addWidget(create);rowButtons->addWidget(remove);rowButtons->addWidget(learnButton);
    rowButtons->addStretch();left->addLayout(rowButtons);

    auto* hint=new QLabel("Message is cc or noteon; channel is 0-15 or any; number is 0-127. "
                          "The action uses the --ctl vocabulary (mute, gain, bypass, denoise) "
                          "or macro <name>. Learn fills the selected row from the next message "
                          "your controller sends.");
    hint->setWordWrap(true);left->addWidget(hint);

    auto* switches=new QHBoxLayout;
    enabled=new QCheckBox("MIDI enabled");enabled->setObjectName("midiEditorEnabled");
    enabled->setChecked(engine.settings.midiEnabled);
    switches->addWidget(enabled);switches->addStretch();left->addLayout(switches);

    statusLine=new QLabel("Edit the bindings, then Save.");statusLine->setObjectName("midiEditorStatus");
    statusLine->setTextFormat(Qt::PlainText);statusLine->setWordWrap(true);outer->addWidget(statusLine);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Save)->setObjectName("midiEditorSave");outer->addWidget(buttons);

    for(const auto& binding:engine.settings.midi.bindings)
        appendRow(binding.id,midiMessageText(binding.message),channelText(binding.channel),
                  QString::number(binding.number),midiActionText(binding.action));
    if(table->rowCount())table->setCurrentCell(0,0);

    connect(create,&QPushButton::clicked,this,[this]{addRow();});
    connect(remove,&QPushButton::clicked,this,[this]{deleteRow();});
    connect(learnButton,&QPushButton::clicked,this,[this]{learn();});
    // Connected after the initial state is applied, so opening the editor cannot
    // look like the user toggled the switch.
    connect(enabled,&QCheckBox::toggled,this,[this](bool on){toggleEnabled(on);});
    connect(buttons,&QDialogButtonBox::accepted,this,[this]{save();});
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
}

MidiEditor::~MidiEditor() {
    // Disarm before the widget goes: the reader may still be holding a copy of the
    // hook it was calling.
    alive->store(false);
    if(input)input->setRawHandler({});
}

QString MidiEditor::status() const {return statusLine?statusLine->text():QString();}
void MidiEditor::report(const QString& text){statusLine->setText(text);}

void MidiEditor::appendRow(const QString& id,const QString& message,const QString& channel,
                           const QString& number,const QString& action) {
    const int row=table->rowCount();
    table->insertRow(row);
    const QStringList cells{id,message,channel,number,action};
    for(int column=0;column<cells.size();++column)table->setItem(row,column,new QTableWidgetItem(cells.at(column)));
}

void MidiEditor::addRow() {
    auto taken=[this](const QString& text){
        for(int row=0;row<table->rowCount();++row)
            if(QString::compare(table->item(row,0)->text(),text,Qt::CaseInsensitive)==0)return true;
        return false;
    };
    QString candidate="binding";int suffix=1;
    while(taken(candidate))candidate=QString("binding %1").arg(++suffix);
    // A new row starts as a valid, harmless binding, so only the message the user
    // wants to change needs editing.
    appendRow(candidate,"cc","any","0","mute mic1 on");
    table->setCurrentCell(table->rowCount()-1,0);
    report(QString("New binding '%1'. Adjust the message, then Save.").arg(candidate));
}

void MidiEditor::deleteRow() {
    const int row=table->currentRow();
    if(row<0){report("Select a binding to delete.");return;}
    const QString removed=table->item(row,0)->text();
    table->removeRow(row);
    if(table->rowCount())table->setCurrentCell(std::min(row,table->rowCount()-1),0);
    report(QString("Deleted '%1'. Save to apply.").arg(removed));
}

void MidiEditor::learn() {
    const int row=table->currentRow();
    if(row<0){report("Select a binding to learn into.");return;}
    // A disabled reader is the normal reason Learn cannot work, and it is the
    // state the checkbox above is in, so name it rather than waiting silently.
    if(!input||input->clientId()<0){
        report("MIDI input is not running; tick \"MIDI enabled\" to learn a message.");
        return;
    }
    learning=true;
    auto flag=alive;
    input->setRawHandler([this,flag](MidiBinding::Message message,int channel,int number,int value){
        if(!flag->load())return;
        // Reader thread: marshal. The queued call is dropped by Qt if the dialog
        // is gone before it runs.
        QMetaObject::invokeMethod(this,[this,message,channel,number,value]{
            learned(message,channel,number,value);
        },Qt::QueuedConnection);
    });
    report("Listening: move the control or press the key you want to bind.");
}

void MidiEditor::learned(MidiBinding::Message message,int channel,int number,int value) {
    Q_UNUSED(value);
    if(!learning)return;
    learning=false;
    if(input)input->setRawHandler({});
    const int row=table->currentRow();
    if(row<0){report("Learned a message, but no binding is selected.");return;}
    table->item(row,1)->setText(midiMessageText(message));
    table->item(row,2)->setText(channelText(channel));
    table->item(row,3)->setText(QString::number(number));
    report(QString("Learned %1 %2 on channel %3 for '%4'. Save to store it.")
        .arg(midiMessageText(message)).arg(number).arg(channelText(channel)).arg(table->item(row,0)->text()));
}

// The switch is live, not draft state: the reader starts or stops as it is
// clicked, and the flag is persisted immediately so a crash before Save cannot
// leave the file saying the opposite of what the reader is doing.
void MidiEditor::toggleEnabled(bool on) {
    engine.settings.midiEnabled=on;
    engine.save();
    if(on) {
        if(!input){report("MIDI enabled. The console starts the reader when it opens.");return;}
        QString error;
        if(input->clientId()<0&&!input->open(error)){
            // The intent stays saved: the sequencer may appear later, and the next
            // start retries it rather than silently dropping the user's choice.
            report("MIDI not started: "+error);
            return;
        }
        input->setBindings(engine.settings.midi);
        report("MIDI enabled. Connect a controller to the \"Linux Mixer MIDI\" port.");
    } else {
        learning=false;
        if(input)input->close();
        report("MIDI disabled; the MIDI reader is stopped.");
    }
}

bool MidiEditor::parseRow(int row,MidiBinding& binding,QString& error) const {
    const auto cell=[this,row](int column){return table->item(row,column)->text().trimmed();};
    const QString id=cell(0);
    if(id.isEmpty()){error="MIDI binding has no id";return false;}
    if(!parseMidiMessage(cell(1),binding.message,error)){error="MIDI binding '"+id+"': "+error;return false;}
    const QString channel=cell(2);
    if(channel.compare("any",Qt::CaseInsensitive)==0)binding.channel=-1;
    else {
        bool ok=false;const int value=channel.toInt(&ok);
        if(!ok){error="MIDI binding '"+id+"': channel '"+channel+"' is not 'any' or 0-15";return false;}
        binding.channel=value;
    }
    bool ok=false;const int number=cell(3).toInt(&ok);
    if(!ok){error="MIDI binding '"+id+"': controller/note number '"+cell(3)+"' is not a whole number";return false;}
    binding.number=number;
    if(!parseMidiAction(cell(4),binding.action,error)){error="MIDI binding '"+id+"': "+error;return false;}
    binding.id=id;
    binding.hasAction=true;
    return true;
}

bool MidiEditor::save() {
    // The range and duplicate-id refusals come from the model, so the messages the
    // user sees are the ones a loaded file would report.
    QList<MidiBinding> parsed;
    for(int row=0;row<table->rowCount();++row) {
        MidiBinding binding;QString error;
        if(!parseRow(row,binding,error)){report("Not saved: "+error);return false;}
        parsed.append(binding);
    }
    QString error;
    if(!validateMidiBindings(parsed,engine.settings,error)){report("Not saved: "+error);return false;}
    engine.settings.midi.bindings=parsed;
    // These described the file as loaded; the stored list is validated now.
    engine.settings.midi.errors.clear();
    engine.settings.midiEnabled=enabled->isChecked();
    engine.apply();engine.save();
    // The reader maps from its own copy, so it is refreshed here rather than
    // waiting for the console to notice the dialog closed.
    if(input)input->setBindings(engine.settings.midi);
    learning=false;
    if(input)input->setRawHandler({});
    // Show the canonical text the next load and the reader will see.
    for(int row=0;row<parsed.size();++row) {
        table->item(row,1)->setText(midiMessageText(parsed[row].message));
        table->item(row,2)->setText(channelText(parsed[row].channel));
        table->item(row,3)->setText(QString::number(parsed[row].number));
        table->item(row,4)->setText(midiActionText(parsed[row].action));
    }
    report(QString("Saved %1 binding(s).").arg(parsed.size()));
    return true;
}

void showMidiEditor(Engine& engine,QWidget* parent,MidiInput* input) {
    MidiEditor editor(engine,input,parent);editor.exec();
}
