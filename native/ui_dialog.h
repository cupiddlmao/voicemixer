#pragma once
// Dialog-side styling kit for the redesigned console.
//
// styleMixerDialog() (console.cpp) owns the dark QPalette and stays untouched;
// this header adds the layer a palette cannot express: the 8px grid, 16px
// dialog margins, 12px gutters, the filled-panel idiom, uppercase tracked
// section headers, status dots and pills, and one primary button per dialog.
//
// Everything here is inline and depends only on Qt and ui_theme.h, so a file
// that includes it can still be compiled into a headless test target that does
// not link console.cpp.
#include "ui_theme.h"

#include <QtGui/QPixmap>
#include <QtGui/QIcon>
#include <QtGui/QPainter>

#include <QtWidgets/QDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLayout>
#include <QtWidgets/QPushButton>

namespace ui {

// Signal meaning, not decoration: accent = active/primary, warn = recording or
// attention, danger = mute/error, neutral = nothing to report.
enum class Tone { Neutral, Active, Warn, Danger, Muted };

inline QColor toneColor(Tone tone) {
    switch (tone) {
        case Tone::Active: return theme::accent;
        case Tone::Warn: return theme::warn;
        case Tone::Danger: return theme::danger;
        case Tone::Neutral: return theme::borderStrong;
        case Tone::Muted: return theme::inkMuted;
    }
    return theme::borderStrong;
}

// Text colour for a tone. Neutral means "an ordinary reading", so it is ink,
// not the border grey toneColor() returns for an unfilled chip.
inline QColor textColor(Tone tone) {
    return tone == Tone::Neutral ? theme::ink : toneColor(tone);
}

// Numeric readouts use the mono companion of the single UI family so columns
// of figures line up without depending on tabular figures in DejaVu Sans.
inline QFont mono(int px, bool bold = false) {
    QFont f("DejaVu Sans Mono");
    f.setPixelSize(px);
    f.setBold(bold);
    return f;
}

// One stylesheet for every dialog, scoped to the dialog it is set on. Rules
// name widget classes and dynamic properties only, so nothing leaks into the
// application or into another dialog.
inline QString dialogQss() {
    return QStringLiteral(R"QSS(
QDialog { background-color: #141d25; color: #e8eff3; }
QFrame[uiPanel="true"] { background-color: #1c2833; border: 1px solid #2d3e4b; border-radius: 6px; }
QLabel { background-color: transparent; color: #e8eff3; }
QPushButton { background-color: #223140; color: #e8eff3; border: 1px solid #2d3e4b;
              border-radius: 4px; padding: 6px 14px; min-height: 16px; }
QPushButton:hover { background-color: #263646; border-color: #3d5262; }
QPushButton:pressed { background-color: #1c2833; }
QPushButton:disabled { color: #6b7f8c; background-color: #1c2833; border-color: #2d3e4b; }
QPushButton[uiRole="primary"] { background-color: #6fd3a8; color: #141d25;
                                border-color: #6fd3a8; font-weight: 600; }
QPushButton[uiRole="primary"]:hover { background-color: #85dcb6; border-color: #85dcb6; }
QPushButton[uiRole="primary"]:pressed { background-color: #5cbf95; }
QPushButton[uiRole="primary"]:disabled { background-color: #2f5a48; color: #6b7f8c; border-color: #2f5a48; }
QDialogButtonBox QPushButton { min-width: 84px; }
QCommandLinkButton { background-color: transparent; border: none; }
QCheckBox, QRadioButton { background-color: transparent; color: #e8eff3; spacing: 8px; padding: 2px 0; }
QCheckBox::indicator, QRadioButton::indicator { width: 14px; height: 14px; }
QCheckBox::indicator { border: 1px solid #3d5262; border-radius: 4px; background-color: #223140; }
QCheckBox::indicator:checked { background-color: #6fd3a8; border-color: #6fd3a8; }
QRadioButton::indicator { border: 1px solid #3d5262; border-radius: 8px; background-color: #223140; }
QRadioButton::indicator:checked { background-color: #6fd3a8; border-color: #6fd3a8; }
QCheckBox:disabled, QRadioButton:disabled { color: #6b7f8c; }
QCheckBox[uiTone="warn"], QRadioButton[uiTone="warn"], QLabel[uiTone="warn"] { color: #e0a340; }
QComboBox { background-color: #223140; color: #e8eff3; border: 1px solid #2d3e4b;
            border-radius: 4px; padding: 3px 8px; min-height: 20px; }
QComboBox:hover { border-color: #3d5262; }
QComboBox:focus { border-color: #6fd3a8; }
QComboBox:disabled { color: #6b7f8c; background-color: #1c2833; border-color: #2d3e4b; }
QComboBox QAbstractItemView { background-color: #223140; color: #e8eff3;
                              border: 1px solid #3d5262; selection-background-color: #2f5a48;
                              selection-color: #e8eff3; outline: 0; }
QLineEdit, QSpinBox, QDoubleSpinBox, QTimeEdit, QAbstractSpinBox {
    background-color: #1c2833; color: #e8eff3; border: 1px solid #2d3e4b;
    border-radius: 4px; padding: 3px 6px; min-height: 20px;
    selection-background-color: #2f5a48; selection-color: #e8eff3; }
/* A stylesheet replaces Fusion's spin arrows with nothing, and Qt cannot draw
   them from CSS borders alone. Verified 2026-11-26 by rendering a styled
   QSpinBox two ways: sub-control rule deleted (leaves an empty button well that
   reads as part of the field) and ::up-arrow/::down-arrow built from
   transparent CSS borders (renders nothing). Real arrows would need image:
   url() from a .qrc resource, i.e. a build-system change for one affordance, so
   the buttons stay folded away and the field reads as a plain numeric entry.
   Typing, the arrow keys and the wheel still change the value. */
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { width: 0; border: none; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QTimeEdit:focus { border-color: #6fd3a8; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QTimeEdit:disabled { color: #6b7f8c; }
QTableView, QTableWidget { background-color: #141d25; alternate-background-color: #1c2833;
                           color: #e8eff3; gridline-color: #223140; border: 1px solid #2d3e4b;
                           border-radius: 6px; selection-background-color: #2f5a48;
                           selection-color: #e8eff3; outline: 0; }
QScrollArea { background-color: #141d25; border: none; }
QScrollArea > QWidget > QWidget { background-color: #141d25; }
QTableView::item, QTableWidget::item { padding: 2px 6px; }
QHeaderView { background-color: transparent; border: none; }
QHeaderView::section { background-color: #1c2833; color: #93a7b4; border: none;
                       border-bottom: 1px solid #2d3e4b; padding: 5px 8px; }
QHeaderView::section:horizontal:last-section { border-right: none; }
QScrollBar:vertical { background-color: #141d25; width: 12px; margin: 0; border: none; }
QScrollBar::handle:vertical { background-color: #2d3e4b; border-radius: 4px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background-color: #3d5262; }
QScrollBar:horizontal { background-color: #141d25; height: 12px; margin: 0; border: none; }
QScrollBar::handle:horizontal { background-color: #2d3e4b; border-radius: 4px; min-width: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; background: none; border: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QToolTip { background-color: #223140; color: #e8eff3; border: 1px solid #3d5262; padding: 4px; }
)QSS");
}

// Grid for the dialog itself: 16px margins, 12px gutters. Call once, right
// after the layout is attached.
inline void decorate(QDialog& dialog) {
    dialog.setStyleSheet(dialogQss());
    if (auto* box = dialog.layout()) {
        box->setContentsMargins(16, 16, 16, 16);
        box->setSpacing(12);
    }
}

// --- Type roles -------------------------------------------------------------

// Section header: uppercase, tracked, secondary ink. Never mixed with a
// sentence-case label in the same row.
inline QLabel* section(const QString& text, QWidget* parent = nullptr) {
    auto* label = new QLabel(text.toUpper(), parent);
    label->setFont(theme::font(theme::type::label, true, true));
    label->setStyleSheet("color: #93a7b4;");
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return label;
}

inline QLabel* title(const QString& text, QWidget* parent = nullptr) {
    auto* label = new QLabel(text, parent);
    label->setFont(theme::font(theme::type::section, true));
    label->setStyleSheet("color: #e8eff3;");
    return label;
}

inline QLabel* body(const QString& text, QWidget* parent = nullptr) {
    auto* label = new QLabel(text, parent);
    label->setFont(theme::font(theme::type::label));
    label->setStyleSheet("color: #e8eff3;");
    label->setWordWrap(true);
    return label;
}

// Supporting prose: same sentence case, one step quieter.
inline QLabel* note(const QString& text, QWidget* parent = nullptr) {
    auto* label = new QLabel(text, parent);
    label->setFont(theme::font(theme::type::value));
    label->setStyleSheet("color: #93a7b4;");
    label->setWordWrap(true);
    return label;
}

// Footnotes and definitions, the quietest thing on the dialog.
inline QLabel* micro(const QString& text, QWidget* parent = nullptr) {
    auto* label = new QLabel(text, parent);
    label->setFont(theme::font(theme::type::micro));
    label->setStyleSheet("color: #6b7f8c;");
    label->setWordWrap(true);
    return label;
}

// A column caption: uppercase micro with tracking, never wrapping. This is the
// console's caption idiom, used to name a column of a table or a readout.
inline QLabel* caption(const QString& text, QWidget* parent = nullptr) {
    auto* label = new QLabel(text.toUpper(), parent);
    label->setFont(theme::font(theme::type::micro, false, true));
    label->setStyleSheet("color: #6b7f8c; background: transparent;");
    label->setWordWrap(false);
    return label;
}

// A figure, or a device name: mono so columns of them line up.
inline QLabel* value(const QString& text, QWidget* parent = nullptr, Tone tone = Tone::Neutral,
                     int px = theme::type::value) {
    auto* label = new QLabel(text, parent);
    label->setFont(mono(px));
    label->setStyleSheet("color: " + textColor(tone).name(QColor::HexRgb) + ";");
    return label;
}

// Field name inside a filled panel: label size, secondary ink.
inline QLabel* field(const QString& text, QWidget* parent = nullptr) {
    auto* label = new QLabel(text, parent);
    label->setFont(theme::font(theme::type::label));
    label->setStyleSheet("color: #93a7b4;");
    return label;
}

// --- Containers -------------------------------------------------------------

// A filled panel that groups related rows instead of boxing every control.
// Attach a layout with one of the helpers below; the panel itself stays bare
// so a caller can choose a grid or a column.
inline QFrame* panel(QWidget* parent = nullptr) {
    auto* box = new QFrame(parent);
    box->setProperty("uiPanel", true);
    return box;
}

inline QVBoxLayout* vboxIn(QWidget* box) {
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    return layout;
}

inline QGridLayout* gridIn(QWidget* box) {
    auto* layout = new QGridLayout(box);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setHorizontalSpacing(18);
    layout->setVerticalSpacing(6);
    return layout;
}

// A caption above a figure, the way the console prints a readout: uppercase
// tracked micro caption, mono value beneath it, both left aligned in a column.
inline void readoutCell(QGridLayout* grid, int col, const QString& caption, QLabel* figure) {
    auto* label = ui::caption(caption);
    grid->addWidget(label, 0, col, Qt::AlignLeft);
    grid->addWidget(figure, 1, col, Qt::AlignLeft);
}

// A hairline that separates a caption band from the rows beneath it.
inline QFrame* rule(QWidget* parent = nullptr) {
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setFixedHeight(1);
    line->setStyleSheet("color: #2d3e4b; background: transparent; border: none;");
    return line;
}

// --- Signal marks -----------------------------------------------------------

inline QLabel* dot(Tone tone, QWidget* parent = nullptr) {
    auto* label = new QLabel(parent);
    QPixmap mark(12, 12);
    mark.fill(Qt::transparent);
    QPainter painter(&mark);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(toneColor(tone));
    painter.drawEllipse(3, 3, 6, 6);
    painter.end();
    label->setPixmap(mark);
    label->setFixedSize(12, 12);
    label->setScaledContents(false);
    return label;
}

// Compact status chip: filled for active and alarming states, outlined when
// the state is merely informational.
inline QLabel* pill(const QString& text, Tone tone, QWidget* parent = nullptr) {
    auto* label = new QLabel(text, parent);
    label->setFont(theme::font(theme::type::value, true));
    label->setAlignment(Qt::AlignCenter);
    const bool filled = tone == Tone::Active || tone == Tone::Warn || tone == Tone::Danger;
    const QString color = toneColor(tone).name(QColor::HexRgb);
    label->setStyleSheet(filled
        ? QStringLiteral("color: #141d25; background-color: %1; border: 1px solid %1;")
              .arg(color)
        : QStringLiteral("color: %1; background-color: transparent; border: 1px solid #3d5262;")
              .arg(color));
    label->setContentsMargins(8, 3, 8, 3);
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return label;
}

// The one primary button on a dialog.
inline QPushButton* primary(QPushButton* button) {
    button->setProperty("uiRole", "primary");
    return button;
}

// Colour-carries-meaning hook for wording that is about arming or recording.
inline void setWarnTone(QWidget* widget) { widget->setProperty("uiTone", "warn"); }

} // namespace ui
