// Macro model, whole-macro validation and settings persistence.
#include "macros.h"
#include "engine.h"
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

    QTextStream(stdout)<<"PASS: macro model, shared step parsing and clamping, whole-macro rejection and settings persistence.\n";
    return 0;
}
