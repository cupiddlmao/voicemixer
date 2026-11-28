#pragma once
#include <QtWidgets/QDialog>
#include <QtCore/QList>
#include <QtCore/QStringList>
#include <atomic>
#include <memory>
#include "engine.h"
#include "midi_input.h"

class QCheckBox;
class QLabel;
class QTableWidget;

// Applies one delivered batch of MIDI actions to the engine's settings, on the GUI
// thread, and commits the whole batch with exactly one engine.apply() and
// engine.save() — the same single-update discipline a macro run keeps, for the same
// reason: the console must never see an intermediate state, and the graph must not
// be poked once per action.
//
// A Macro action is resolved by name against engine.settings at the moment it runs
// and goes through runMacro, so a macro edited since the binding was saved runs its
// current steps. A name that no longer exists is skipped and named on the existing
// status line; it is never a dialog, because this runs while the user is turning a
// knob and the console may not even be visible.
//
// Lives here, beside the editor, so the console and the offscreen test drive the
// same function rather than two copies of the loop.
void applyMidiBatch(Engine& engine,const QList<MidiAction>& actions);

// Plain MIDI binding editor in the existing dialog style: one editable row per
// binding (id, message, channel, controller or note number, action), New/Delete,
// Learn for the selected row, a "MIDI enabled" checkbox that starts and stops the
// reader live, Save and one status line naming the first failing binding.
//
// Edits live only in the table until Save, which parses every row and validates the
// whole list with validateMidiBindings against the saved macros. A refusal leaves
// engine.settings and settings.json untouched and puts the model's own message on
// the status line — no modal dialog, exactly like the macro editor.
//
// input is the console's single reader; the editor never owns one. A null reader
// means there is nothing to learn from and nothing to start, which is why Learn
// says so instead of silently doing nothing.
class MidiEditor : public QDialog {
public:
    explicit MidiEditor(Engine& engine,MidiInput* input=nullptr,QWidget* parent=nullptr);
    ~MidiEditor() override;
    // Commits the table; false leaves engine.settings and settings.json untouched.
    bool save();
    QString status() const;
private:
    Engine& engine;
    MidiInput* input=nullptr;
    QTableWidget* table=nullptr;
    QCheckBox* enabled=nullptr;
    QLabel* statusLine=nullptr;
    // The learn hook runs on the reader thread and only marshals; this flag is how
    // a hook already in flight learns that the editor is gone instead of touching a
    // dead widget.
    std::shared_ptr<std::atomic<bool>> alive;
    bool learning=false;
    void appendRow(const QString& id,const QString& message,const QString& channel,
                   const QString& number,const QString& action);
    void addRow();
    void deleteRow();
    void learn();
    void learned(MidiBinding::Message message,int channel,int number,int value);
    void toggleEnabled(bool on);
    bool parseRow(int row,MidiBinding& binding,QString& error) const;
    void report(const QString& text);
};
void showMidiEditor(Engine& engine,QWidget* parent=nullptr,MidiInput* input=nullptr);
