#pragma once
#include <QtWidgets/QDialog>
#include <QtCore/QList>
#include <QtCore/QStringList>
#include "engine.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

// Plain macro editor in the existing dialog style: the macro list on the left,
// editable step rows on the right, New/Delete, Add/Remove step, Rename, Run and
// Test, and one status line that names the first failing step.
//
// Edits accumulate in a draft list, so nothing reaches engine.settings until
// Save validates the whole draft with validateMacro against the other macros
// (duplicate names and bad steps are refused with the specific error) and then
// issues exactly one engine.apply() and engine.save(). Run applies the selected
// macro's steps through runMacro and saves; Test only validates, and neither
// stores the edited definition — only Save does.
//
// Deleting or renaming a macro that a queued --ctl command still names is not
// compensated for here: the drain path already reports the unknown macro when
// that command runs.
class MacroEditor : public QDialog {
public:
    explicit MacroEditor(Engine& engine,QWidget* parent=nullptr);
    // Commits the draft; false leaves engine.settings and settings.json untouched.
    bool save();
    QString status() const;
private:
    struct Draft { QString name; QStringList steps; };
    Engine& engine;
    QList<Draft> draft;
    int current=-1;
    QListWidget* macroList=nullptr;
    QLineEdit* nameEdit=nullptr;
    QListWidget* stepList=nullptr;
    QLabel* toggleWarning=nullptr;
    QLabel* statusLine=nullptr;
    QPushButton* renameButton=nullptr;
    void store(int index);
    void load();
    void reindex();
    void newMacro();
    void deleteMacro();
    void renameMacro();
    void addStep();
    void removeStep();
    void run();
    void test();
    void refreshWarning();
    bool parseDraft(int index,Macro& macro,QString& error) const;
    bool validateDraft(int index,Macro& macro,QString& error) const;
    void report(const QString& text);
};
void showMacroEditor(Engine& engine,QWidget* parent=nullptr);
