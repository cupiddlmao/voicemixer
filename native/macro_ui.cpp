#include "macro_ui.h"
#include "console.h"
#include "macros.h"
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtCore/QSignalBlocker>
#include <algorithm>

namespace {
// Canonical text for every step, so a row shows what macroStepText would write
// to settings.json and what --ctl would accept back.
QStringList stepTexts(const Macro& macro) {
    QStringList texts;for(const auto& step:macro.steps)texts.append(macroStepText(step));return texts;
}
}

MacroEditor::MacroEditor(Engine& engine,QWidget* parent):QDialog(parent),engine(engine) {
    setObjectName("macroEditor");setWindowTitle("Macro editor");resize(820,560);styleMixerDialog(*this);
    for(const auto& macro:engine.settings.macros.macros)draft.append({macro.name,stepTexts(macro)});

    auto* outer=new QVBoxLayout(this);auto* body=new QHBoxLayout;outer->addLayout(body,1);
    auto* left=new QVBoxLayout;body->addLayout(left,1);
    left->addWidget(new QLabel("Macros"));
    macroList=new QListWidget;macroList->setObjectName("macroEditorList");macroList->setMinimumWidth(190);left->addWidget(macroList,1);
    auto* macroButtons=new QHBoxLayout;
    auto* create=new QPushButton("New macro");create->setObjectName("macroEditorNew");
    auto* remove=new QPushButton("Delete macro");remove->setObjectName("macroEditorDelete");
    macroButtons->addWidget(create);macroButtons->addWidget(remove);left->addLayout(macroButtons);

    auto* right=new QVBoxLayout;body->addLayout(right,2);
    auto* nameRow=new QHBoxLayout;
    nameRow->addWidget(new QLabel("Name"));
    nameEdit=new QLineEdit;nameEdit->setObjectName("macroEditorName");nameEdit->setPlaceholderText("Macro name");
    renameButton=new QPushButton("Rename");renameButton->setObjectName("macroEditorRename");
    nameRow->addWidget(nameEdit,1);nameRow->addWidget(renameButton);right->addLayout(nameRow);
    auto* hint=new QLabel("Steps use the --ctl vocabulary: mute, gain, bypass, denoise. For example: gain mic1 -6");
    hint->setWordWrap(true);right->addWidget(hint);
    stepList=new QListWidget;stepList->setObjectName("macroEditorSteps");
    stepList->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::EditKeyPressed|QAbstractItemView::SelectedClicked);
    right->addWidget(stepList,1);
    auto* stepButtons=new QHBoxLayout;
    auto* add=new QPushButton("Add step");add->setObjectName("macroEditorAddStep");
    auto* discard=new QPushButton("Remove step");discard->setObjectName("macroEditorRemoveStep");
    stepButtons->addWidget(add);stepButtons->addWidget(discard);stepButtons->addStretch();right->addLayout(stepButtons);
    // One label, not one per row: the warning is about the macro as a whole.
    toggleWarning=new QLabel;toggleWarning->setObjectName("macroEditorToggleWarning");toggleWarning->setWordWrap(true);right->addWidget(toggleWarning);
    auto* actions=new QHBoxLayout;
    auto* runButton=new QPushButton("Run");runButton->setObjectName("macroEditorRun");
    auto* testButton=new QPushButton("Test");testButton->setObjectName("macroEditorTest");
    actions->addWidget(runButton);actions->addWidget(testButton);actions->addStretch();right->addLayout(actions);

    statusLine=new QLabel("Edit a macro, then Save.");statusLine->setObjectName("macroEditorStatus");
    statusLine->setTextFormat(Qt::PlainText);statusLine->setWordWrap(true);outer->addWidget(statusLine);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Save)->setObjectName("macroEditorSave");outer->addWidget(buttons);

    connect(create,&QPushButton::clicked,this,[this]{newMacro();});
    connect(remove,&QPushButton::clicked,this,[this]{deleteMacro();});
    connect(renameButton,&QPushButton::clicked,this,[this]{renameMacro();});
    connect(add,&QPushButton::clicked,this,[this]{addStep();});
    connect(discard,&QPushButton::clicked,this,[this]{removeStep();});
    connect(runButton,&QPushButton::clicked,this,[this]{run();});
    connect(testButton,&QPushButton::clicked,this,[this]{test();});
    connect(buttons,&QDialogButtonBox::accepted,this,[this]{save();});
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    // The warning follows the rows being typed, so it cannot go stale.
    connect(stepList,&QListWidget::itemChanged,this,[this]{refreshWarning();});
    connect(macroList,&QListWidget::currentRowChanged,this,[this](int row){
        if(row==current)return;
        store(current);current=row;load();
    });
    current=draft.isEmpty()?-1:0;
    reindex();load();
}

QString MacroEditor::status() const {return statusLine?statusLine->text():QString();}
void MacroEditor::report(const QString& text){statusLine->setText(text);}

// Widgets -> draft. Called before any operation that needs the rows as typed:
// selection changes, Save, Run, Test, Rename, New and Delete.
void MacroEditor::store(int index) {
    if(index<0||index>=draft.size())return;
    draft[index].name=nameEdit->text().trimmed();
    QStringList steps;
    for(int i=0;i<stepList->count();++i)steps.append(stepList->item(i)->text().simplified());
    draft[index].steps=steps;
}

// Draft -> widgets. Rebuilding the rows emits itemChanged, so guard them.
void MacroEditor::load() {
    const bool selected=current>=0&&current<draft.size();
    nameEdit->setEnabled(selected);renameButton->setEnabled(selected);stepList->setEnabled(selected);
    QSignalBlocker block(stepList);
    stepList->clear();
    nameEdit->setText(selected?draft[current].name:QString());
    if(selected)for(const auto& step:draft[current].steps)stepList->addItem(step);
    if(stepList->count())stepList->setCurrentRow(0);
    refreshWarning();
}

// Names only; the current row is restored without emitting a selection change.
void MacroEditor::reindex() {
    QSignalBlocker block(macroList);
    macroList->clear();
    for(const auto& entry:draft)macroList->addItem(entry.name);
    macroList->setCurrentRow(current>=0&&current<draft.size()?current:-1);
}

bool MacroEditor::parseDraft(int index,Macro& macro,QString& error) const {
    if(index<0||index>=draft.size()){error="no macro selected";return false;}
    macro.name=draft[index].name.trimmed();macro.steps.clear();
    for(int i=0;i<draft[index].steps.size();++i) {
        ControlCommand step;QString stepError;
        if(!parseMacroStep(draft[index].steps.at(i),step,stepError)) {
            error=QString("macro '%1' step %2: %3").arg(macro.name).arg(i+1).arg(stepError);
            return false;
        }
        macro.steps.append(step);
    }
    return true;
}

// validateMacro owns the specific wording for duplicate names and unsound
// steps. The other drafts contribute their names only, so one unfinished macro
// cannot hide the error in the macro being validated.
bool MacroEditor::validateDraft(int index,Macro& macro,QString& error) const {
    if(!parseDraft(index,macro,error))return false;
    QList<Macro> others;
    for(int i=0;i<draft.size();++i)if(i!=index){Macro other;other.name=draft[i].name.trimmed();others.append(other);}
    return validateMacro(macro,others,error);
}

void MacroEditor::refreshWarning() {
    Macro macro;macro.name=nameEdit->text().trimmed();
    for(int i=0;i<stepList->count();++i) {
        ControlCommand step;QString error;
        if(parseMacroStep(stepList->item(i)->text(),step,error))macro.steps.append(step);
    }
    toggleWarning->setText(macroContainsToggle(macro)?"Contains a toggle: running this macro twice flips it back.":QString());
}

void MacroEditor::newMacro() {
    store(current);
    auto taken=[this](const QString& text){
        for(const auto& entry:draft)if(QString::compare(entry.name,text,Qt::CaseInsensitive)==0)return true;
        return false;
    };
    QString candidate="Macro";int suffix=1;
    while(taken(candidate))candidate=QString("Macro %1").arg(++suffix);
    // A new macro starts runnable: one harmless default step, so only the name
    // and the steps the user wants to change need editing.
    draft.append({candidate,{"mute mic1 on"}});
    current=draft.size()-1;reindex();load();
    nameEdit->setFocus();nameEdit->selectAll();
    report(QString("New macro '%1'. Add steps, then Save.").arg(candidate));
}

void MacroEditor::deleteMacro() {
    if(current<0||current>=draft.size()){report("Select a macro to delete.");return;}
    store(current);
    const QString removed=draft.takeAt(current).name;
    current=draft.isEmpty()?-1:std::min(current,int(draft.size())-1);
    reindex();load();
    report(QString("Deleted '%1'. Save to apply.").arg(removed));
}

void MacroEditor::renameMacro() {
    if(current<0||current>=draft.size()){report("Select a macro to rename.");return;}
    store(current);
    Macro macro;QString error;
    if(!validateDraft(current,macro,error)) {
        // Keep list and field agreeing with what is actually stored.
        nameEdit->setText(draft[current].name);
        report("Rename refused: "+error);
        return;
    }
    draft[current].name=macro.name;reindex();
    report(QString("Renamed to '%1'. Save to apply.").arg(macro.name));
}

void MacroEditor::addStep() {
    if(current<0||current>=draft.size()){report("Create or select a macro first.");return;}
    stepList->addItem("mute mic1 on");
    stepList->setCurrentRow(stepList->count()-1);
    refreshWarning();
    report("Step added. Save to store the macro.");
}

void MacroEditor::removeStep() {
    if(current<0||stepList->count()==0){report("There is no step to remove.");return;}
    const int row=std::max(0,stepList->currentRow());
    delete stepList->takeItem(row);
    if(stepList->count())stepList->setCurrentRow(std::min(row,stepList->count()-1));
    refreshWarning();
    report("Step removed. Save to store the macro.");
}

void MacroEditor::run() {
    store(current);
    Macro macro;QString error;
    if(current<0){report("Select a macro to run.");return;}
    if(!validateDraft(current,macro,error)){report("Not run: "+error);return;}
    if(!runMacro(macro,engine.settings,&error)){report("Not run: "+error);return;}
    engine.apply();engine.save();
    report(QString("Ran '%1' · %2 step(s)").arg(macro.name).arg(macro.steps.size())
           +(macroContainsToggle(macro)?" · contains toggle: running it twice flips it back":QString()));
}

// Validation only: no settings change, no apply, no save.
void MacroEditor::test() {
    store(current);
    Macro macro;QString error;
    if(current<0){report("Select a macro to test.");return;}
    if(!validateDraft(current,macro,error)){report("Test failed: "+error);return;}
    report(QString("Test passed: '%1' · %2 step(s); not saved").arg(macro.name).arg(macro.steps.size())
           +(macroContainsToggle(macro)?" · contains toggle: running it twice flips it back":QString()));
}

bool MacroEditor::save() {
    store(current);
    QList<Macro> parsed;
    for(int i=0;i<draft.size();++i) {
        Macro macro;QString error;
        if(!validateDraft(i,macro,error)){report("Not saved: "+error);return false;}
        parsed.append(macro);
    }
    engine.settings.macros.macros=parsed;
    // These described the file as loaded; the stored list is validated now.
    engine.settings.macros.errors.clear();
    // Show the canonical text the next load and --ctl will see.
    for(int i=0;i<parsed.size();++i) {
        draft[i].name=parsed[i].name;
        draft[i].steps.clear();
        for(const auto& step:parsed[i].steps)draft[i].steps.append(macroStepText(step));
    }
    engine.apply();engine.save();
    reindex();load();
    report(QString("Saved %1 macro(s).").arg(parsed.size()));
    return true;
}

void showMacroEditor(Engine& engine,QWidget* parent) {
    MacroEditor editor(engine,parent);editor.exec();
}
