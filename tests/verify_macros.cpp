// Macro model, whole-macro validation and settings persistence.
#include "macros.h"
#include "engine.h"
#include "macro_ui.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextStream>
#include <initializer_list>

#define CHECK(x) do{if(!(x)){QTextStream(stderr)<<"FAIL line "<<__LINE__<<": "#x"\n";return 1;}}while(0)

static QJsonObject macroJson(const QString& name,const QStringList& steps){
    QJsonArray list;for(const auto& step:steps)list.append(step);
    QJsonObject object;object.insert("name",name);object.insert("steps",list);return object;
}
static QJsonArray macrosArray(std::initializer_list<QJsonObject> entries){
    QJsonArray list;for(const auto& entry:entries)list.append(entry);return list;
}
static QJsonObject settingsWith(const QJsonArray& macros){
    QJsonObject object;object.insert("macros",macros);return object;
}

int runMacroTests(){
    // A well-formed macro keeps its name and step order in the shared ControlCommand form.
    Settings loaded;
    loaded.load(settingsWith(macrosArray({macroJson("Stream",{"mute music on","gain mic1 -6","denoise mic1 on"})})));
    CHECK(loaded.macros.errors.isEmpty());
    CHECK(loaded.macros.macros.size()==1);
    const Macro& macro=loaded.macros.macros.first();
    CHECK(macro.name=="Stream");
    CHECK(macro.steps.size()==3);
    CHECK(macro.steps[0].verb==ControlCommand::Mute&&macro.steps[0].kind==ControlCommand::Input&&macro.steps[0].index==1&&macro.steps[0].mode==ControlCommand::On);
    CHECK(macro.steps[1].verb==ControlCommand::Gain&&macro.steps[1].kind==ControlCommand::Mic&&macro.steps[1].index==0&&macro.steps[1].db==-6);
    CHECK(macro.steps[2].verb==ControlCommand::Denoise&&macro.steps[2].kind==ControlCommand::Mic&&macro.steps[2].index==0&&macro.steps[2].mode==ControlCommand::On);

    // Settings::json() -> load() round-trip preserves the macros.
    Settings roundTrip;roundTrip.load(loaded.json());
    CHECK(roundTrip.macros.errors.isEmpty());
    CHECK(roundTrip.macros.macros.size()==1);
    CHECK(roundTrip.macros.macros[0].name=="Stream");
    CHECK(roundTrip.macros.macros[0].steps.size()==3);
    for(int i=0;i<3;++i){
        const auto& a=macro.steps[i];const auto& b=roundTrip.macros.macros[0].steps[i];
        CHECK(a.verb==b.verb&&a.kind==b.kind&&a.index==b.index&&a.mode==b.mode&&a.db==b.db);
    }

    // Legacy settings without a "macros" key load cleanly.
    Settings legacy;legacy.load(QJsonObject{});
    CHECK(legacy.macros.macros.isEmpty()&&legacy.macros.errors.isEmpty());
    Settings emptyList;emptyList.load(settingsWith(QJsonArray{}));
    CHECK(emptyList.macros.macros.isEmpty()&&emptyList.macros.errors.isEmpty());

    // Out-of-range gain is clamped by the shared CLI parser.
    Settings clamped;
    clamped.load(settingsWith(macrosArray({macroJson("Loud",{"gain mic1 99","gain mic2 -999"})})));
    CHECK(clamped.macros.errors.isEmpty());
    CHECK(clamped.macros.macros.size()==1);
    CHECK(clamped.macros.macros[0].steps.size()==2);
    CHECK(clamped.macros.macros[0].steps[0].db==12);
    CHECK(clamped.macros.macros[0].steps[1].db==-60);

    // Unknown target, unknown verb and an empty step list each reject the macro.
    Settings badTarget;
    badTarget.load(settingsWith(macrosArray({macroJson("Bad target",{"gain mic9 -6"})})));
    CHECK(badTarget.macros.macros.isEmpty()&&!badTarget.macros.errors.isEmpty());
    Settings badVerb;
    badVerb.load(settingsWith(macrosArray({macroJson("Bad verb",{"explode mic1"})})));
    CHECK(badVerb.macros.macros.isEmpty()&&!badVerb.macros.errors.isEmpty());
    Settings noSteps;
    noSteps.load(settingsWith(macrosArray({macroJson("Empty",QStringList{})})));
    CHECK(noSteps.macros.macros.isEmpty()&&!noSteps.macros.errors.isEmpty());

    // A duplicate macro name is dropped, keeping the first definition.
    Settings duplicate;
    duplicate.load(settingsWith(macrosArray({macroJson("Speak",{"mute mic1 on"}),macroJson("Speak",{"mute mic2 on"})})));
    CHECK(duplicate.macros.macros.size()==1);
    CHECK(duplicate.macros.macros[0].name=="Speak");
    CHECK(duplicate.macros.errors.size()==1);

    // A malformed entry is dropped on its own; well-formed neighbours survive.
    Settings mixed;
    mixed.load(settingsWith(macrosArray({macroJson("Good",{"mute music on"}),macroJson("Bad",{"mute mic9"}),macroJson("Also good",{"gain aux -3"})})));
    CHECK(mixed.macros.macros.size()==2);
    CHECK(mixed.macros.errors.size()==1);
    CHECK(mixed.macros.macros[0].name=="Good"&&mixed.macros.macros[1].name=="Also good");

    // One invalid step rejects the whole macro, and its valid steps are not applied.
    Settings partial;
    partial.load(settingsWith(macrosArray({macroJson("Half",{"gain mic1 -6","gain mic9 -6"})})));
    CHECK(partial.macros.macros.isEmpty());
    CHECK(partial.macros.errors.size()==1);
    CHECK(partial.hardware(0).gain==0);
    Macro mixedSteps;mixedSteps.name="Half";
    ControlCommand valid;QString error;
    CHECK(parseMacroStep("gain mic1 -6",valid,error));
    mixedSteps.steps.append(valid);
    ControlCommand invalid;invalid.kind=ControlCommand::Mic;invalid.index=9;
    mixedSteps.steps.append(invalid);
    CHECK(!validateMacro(mixedSteps,QList<Macro>{},error));
    CHECK(!error.isEmpty());

    // Validation also enforces a non-empty name, at least one step and a unique name.
    Macro unnamed;CHECK(!validateMacro(unnamed,QList<Macro>{},error));
    Macro silent;silent.name="Silent";CHECK(!validateMacro(silent,QList<Macro>{},error));
    Macro duplicateMacro;duplicateMacro.name="speak";duplicateMacro.steps.append(valid);
    CHECK(!validateMacro(duplicateMacro,duplicate.macros.macros,error));

    // Toggle steps are accepted, but a computed marker reports them so an editor
    // can warn that running the macro twice flips twice.
    Macro toggleMacro;toggleMacro.name="Flip";
    ControlCommand toggleStep;CHECK(parseMacroStep("mute mic1 toggle",toggleStep,error));
    toggleMacro.steps.append(toggleStep);
    CHECK(validateMacro(toggleMacro,QList<Macro>{},error));
    CHECK(macroContainsToggle(toggleMacro));
    Macro explicitMacro;explicitMacro.name="Explicit";
    ControlCommand onStep;CHECK(parseMacroStep("mute music on",onStep,error));
    explicitMacro.steps.append(onStep);
    CHECK(!macroContainsToggle(explicitMacro));
    // Gain has no on/off meaning and keeps Mode at its default; it is not a toggle.
    Macro gainOnly;gainOnly.name="Trim";
    ControlCommand trim;CHECK(parseMacroStep("gain aux -3",trim,error));
    gainOnly.steps.append(trim);
    CHECK(!macroContainsToggle(gainOnly));

    // A successful run lands every step on the one Settings object handed in.
    Settings applied;
    applied.routing.inputMute.at(1)=false;
    applied.hardware(0).gain=0;
    Macro session;session.name="Session";
    for(const QString& text:QStringList{"mute music on","gain mic1 -6","denoise mic1 on","bypass mic2 on"}){
        ControlCommand step;QString parseError;
        CHECK(parseMacroStep(text,step,parseError));
        session.steps.append(step);
    }
    QString runError="stale";
    CHECK(runMacro(session,applied,&runError));
    CHECK(runError.isEmpty());
    CHECK(applied.routing.inputMute.at(1));
    CHECK(applied.hardware(0).gain==-6);
    CHECK(applied.hardware(0).denoise);
    CHECK(applied.hardware(1).bypass);

    // The partial-application guard: a failing second step leaves every setting
    // exactly as it was, including the first step's would-be change.
    Settings guarded;guarded.hardware(0).gain=3;guarded.routing.inputMute.at(1)=false;
    const QJsonObject before=guarded.json();
    Macro half;half.name="Half";
    ControlCommand firstStep;CHECK(parseMacroStep("gain mic1 -6",firstStep,error));
    half.steps.append(firstStep);
    ControlCommand missingTarget;missingTarget.verb=ControlCommand::Gain;
    missingTarget.kind=ControlCommand::Mic;missingTarget.index=9;missingTarget.db=-6;
    half.steps.append(missingTarget);
    runError.clear();
    CHECK(!runMacro(half,guarded,&runError));
    CHECK(runError.contains("step 2"));
    CHECK(guarded.json()==before);

    // A toggle step runs, and running the same macro again flips it back.
    Settings flipped;flipped.hardware(0).mute=false;
    Macro flip;flip.name="Flip";
    ControlCommand flipStep;CHECK(parseMacroStep("mute mic1 toggle",flipStep,error));
    flip.steps.append(flipStep);
    CHECK(runMacro(flip,flipped,&runError));
    CHECK(flipped.hardware(0).mute);
    CHECK(runMacro(flip,flipped,&runError));
    CHECK(!flipped.hardware(0).mute);

    QTextStream(stdout)<<"PASS: macro model, shared step parsing and clamping, whole-macro rejection and settings persistence.\n";
    return 0;
}

// The editor itself, opened offscreen and driven through its real widgets. The
// engine reads settings from a temporary state directory, so saved user
// settings are never touched.
int runMacroEditorTest(QApplication& app) {
    QTemporaryDir state;
    CHECK(state.isValid());
    qputenv("LINUX_MIXER_STATE_DIR",state.path().toUtf8());
    Engine engine;
    CHECK(engine.settings.macros.macros.isEmpty());
    MacroEditor editor(engine);
    editor.show();
    app.processEvents();

    auto* macroList=editor.findChild<QListWidget*>("macroEditorList");
    auto* nameEdit=editor.findChild<QLineEdit*>("macroEditorName");
    auto* stepList=editor.findChild<QListWidget*>("macroEditorSteps");
    auto* newButton=editor.findChild<QPushButton*>("macroEditorNew");
    auto* deleteButton=editor.findChild<QPushButton*>("macroEditorDelete");
    auto* addButton=editor.findChild<QPushButton*>("macroEditorAddStep");
    auto* saveButton=editor.findChild<QPushButton*>("macroEditorSave");
    auto* status=editor.findChild<QLabel*>("macroEditorStatus");
    auto* warning=editor.findChild<QLabel*>("macroEditorToggleWarning");
    CHECK(macroList&&nameEdit&&stepList&&newButton&&deleteButton&&addButton&&saveButton&&status&&warning);

    // A new macro with two steps, typed into the widgets the user sees.
    newButton->click();
    CHECK(macroList->count()==1);
    CHECK(stepList->count()==1);
    nameEdit->setText("Stream");
    addButton->click();
    CHECK(stepList->count()==2);
    stepList->item(0)->setText("gain mic1 -6");
    // A toggle step raises the single run-twice warning from macroContainsToggle.
    stepList->item(1)->setText("mute music toggle");
    CHECK(warning->text().contains("twice"));
    stepList->item(1)->setText("mute music on");
    CHECK(warning->text().isEmpty());

    // One Save is one engine apply cycle, and the settings carry the macro.
    int applies=0;const auto previous=engine.changed;
    engine.changed=[&applies,previous]{++applies;if(previous)previous();};
    saveButton->click();
    engine.changed=previous;
    CHECK(applies==1);
    CHECK(engine.settings.macros.macros.size()==1);
    CHECK(engine.settings.macros.macros.first().name=="Stream");
    const Macro& saved=engine.settings.macros.macros.first();
    CHECK(saved.steps.size()==2);
    CHECK(macroStepText(saved.steps[0])=="gain mic1 -6"&&saved.steps[0].db==-6);
    CHECK(macroStepText(saved.steps[1])=="mute music on"&&saved.steps[1].mode==ControlCommand::On);
    {
        QFile file(state.path()+"/settings.json");
        CHECK(file.open(QIODevice::ReadOnly));
        const auto macros=QJsonDocument::fromJson(file.readAll()).object().value("macros").toArray();
        CHECK(macros.size()==1);
        CHECK(macros[0].toObject().value("name").toString()=="Stream");
        CHECK(macros[0].toObject().value("steps").toArray().size()==2);
    }

    // A duplicate name is refused with the specific error and nothing is saved.
    newButton->click();
    nameEdit->setText("stream");
    saveButton->click();
    CHECK(status->text().contains("duplicate macro name"));
    CHECK(engine.settings.macros.macros.size()==1);
    CHECK(engine.settings.macros.macros.first().name=="Stream");

    // A bad step is refused with its position, and the draft is not saved.
    nameEdit->setText("Bright");
    addButton->click();
    stepList->item(1)->setText("gain mic9 -6");
    saveButton->click();
    CHECK(status->text().contains("step 2"));
    CHECK(engine.settings.macros.macros.size()==1);
    CHECK(engine.settings.macros.macros.first().name=="Stream");

    // Deleting the saved macro removes it on the next Save.
    macroList->setCurrentRow(0);
    deleteButton->click();
    CHECK(macroList->count()==1);
    CHECK(nameEdit->text()=="Bright");
    stepList->item(1)->setText("mute music on");
    saveButton->click();
    CHECK(status->text().contains("Saved"));
    CHECK(engine.settings.macros.macros.size()==1);
    CHECK(engine.settings.macros.macros.first().name=="Bright");

    // Deleting the last macro leaves no "macros" key in settings.json.
    macroList->setCurrentRow(0);
    deleteButton->click();
    saveButton->click();
    CHECK(engine.settings.macros.macros.isEmpty());
    {
        QFile file(state.path()+"/settings.json");
        CHECK(file.open(QIODevice::ReadOnly));
        CHECK(!QJsonDocument::fromJson(file.readAll()).object().contains("macros"));
    }

    editor.close();
    QTextStream(stdout)<<"PASS: macro editor opened offscreen; New/Add/Rename/Save, duplicate-name and bad-step refusal, the toggle warning and deletion all behave; Save used one engine apply cycle.\n";
    return 0;
}
