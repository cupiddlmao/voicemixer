#include "console.h"
#include "audio_health.h"
#include "update_checker.h"
#include "ui_dialog.h"
#include <QtWidgets/QApplication>
#include <QtCore/QDir>
#include <QtCore/QTextStream>
#include "plugin_racks.h"
#include <QtWidgets/QDialog>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QDialogButtonBox>
#include <QtCore/QTimer>
#include <array>
#include <sys/resource.h>

namespace {

// A figure and the caption that names it. Colour is signal only: neutral for a
// plain reading, danger when the number is an error count worth noticing.
void setReadout(QLabel* cell, const QString& text, ui::Tone tone) {
    cell->setText(text);
    cell->setStyleSheet("color: " + ui::textColor(tone).name(QColor::HexRgb) + "; background: transparent;");
}

// PipeWire reports a single-letter node state. R, T and t are the live ones;
// the health monitor counts errors on those and only those.
bool nodeLive(const QString& state) { return state == "R" || state == "T" || state == "t"; }

QIcon nodeMark(ui::Tone tone) {
    QPixmap mark(12, 12);
    mark.fill(Qt::transparent);
    QPainter painter(&mark);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ui::toneColor(tone));
    painter.drawEllipse(3, 3, 6, 6);
    painter.end();
    return QIcon(mark);
}

} // namespace

void Console::audioHealth(){
    QDialog dialog(this);dialog.setWindowTitle("Audio health");dialog.resize(790,530);styleMixerDialog(dialog);
    auto* layout=new QVBoxLayout(&dialog);ui::decorate(dialog);
    // A section header sits tight above what it names; the 12px gutter belongs
    // between groups, not between a header and its content.
    const auto group=[&](QLabel* header,QWidget* content){
        auto* box=new QVBoxLayout;box->setSpacing(6);
        box->addWidget(header);box->addWidget(content);
        layout->addLayout(box);
    };

    // Header: what this window reports, and whether the monitor is talking.
    auto* header=new QHBoxLayout;header->setSpacing(8);
    header->addWidget(ui::title("Audio statistics"));
    auto* statusDot=ui::dot(ui::Tone::Warn);
    header->addWidget(statusDot);
    auto* statusLine=ui::note("Waiting for audio statistics");
    header->addWidget(statusLine,1);
    auto* statusPill=ui::pill("OFFLINE",ui::Tone::Warn);
    header->addWidget(statusPill);
    layout->addLayout(header);

    // Signal path: the five figures an operator reads off this panel.
    std::array<QLabel*,5> figures{};
    {
        const QStringList captions{"Sample rate","Quantum","Period","Busiest driver","Node errors"};
        auto* signal=ui::panel();
        auto* grid=ui::gridIn(signal);
        for(int col=0;col<captions.size();++col){
            figures[col]=ui::value("—",nullptr,ui::Tone::Muted,theme::type::label);
            ui::readoutCell(grid,col,captions[col],figures[col]);
            // Even columns: the strip uses the panel's width instead of
            // trailing off into empty space on the right.
            grid->setColumnStretch(col,1);
        }
        group(ui::section("Signal path"),signal);
    }

    // Nodes: one 28px row per node, striped, state carried by a dot rather
    // than by coloured words.
    auto* nodesHeader=ui::section("Nodes");
    auto* table=new QTableWidget;table->setColumnCount(4);table->setHorizontalHeaderLabels({"Node","Load","Errors since reset","Node counter"});
    table->setAlternatingRowColors(true);table->setShowGrid(false);table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->setDefaultSectionSize(int(theme::rowHeight));table->verticalHeader()->hide();
    table->horizontalHeader()->setFixedHeight(int(theme::rowHeight));table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);table->horizontalHeader()->setSectionResizeMode(3,QHeaderView::ResizeToContents);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Four whole rows: a node cut in half at the fold reads as a broken panel.
    table->setFixedHeight(int(theme::rowHeight)*(1+4));
    for(int col=0;col<4;++col)table->horizontalHeaderItem(col)->setTextAlignment(col?Qt::AlignRight|Qt::AlignVCenter:Qt::AlignLeft|Qt::AlignVCenter);
    // With PipeWire down the table would be a silent void; say so in it.
    auto* emptyNote=new QLabel("No nodes to report",table->viewport());
    emptyNote->setAlignment(Qt::AlignCenter);
    emptyNote->setFont(theme::font(theme::type::value));
    emptyNote->setStyleSheet("color: #6b7f8c; background: transparent;");
    emptyNote->setGeometry(table->viewport()->rect());
    emptyNote->raise();
    group(nodesHeader,table);

    // Controller: two facts about this process, on one line.
    std::array<QLabel*,2> controllerFigures{};
    {
        auto* controller=ui::panel();
        auto* row=ui::vboxIn(controller);row->setSpacing(0);
        auto* line=new QHBoxLayout;line->setSpacing(24);
        const QStringList captions{"Memory-lock limit","Failed plugin racks"};
        for(int i=0;i<2;++i){
            auto* cell=new QVBoxLayout;cell->setSpacing(2);
            controllerFigures[i]=ui::value("—",nullptr,ui::Tone::Neutral,theme::type::label);
            cell->addWidget(ui::caption(captions[i]));cell->addWidget(controllerFigures[i]);
            line->addLayout(cell,1);
        }
        row->addLayout(line);
        group(ui::section("Controller"),controller);
    }

    // Footnote, then actions: only Close is a primary action here.
    auto* explanation=ui::micro("Load is the fraction of an audio processing period used, not total CPU usage. Errors count PipeWire node errors after the first sample; one interruption can affect several nodes. Buffer time is one graph period, not end-to-end latency. The node counter can restart when a device restarts; session totals keep accumulated errors. Statistics are read continuously; the first two seconds establish a baseline. Hung commands time out automatically. Unresponsive plugin racks restore normal input routing; click Plugins on that input to retry.");
    layout->addWidget(explanation);
    // Any slack the window manager leaves goes above the buttons, never into
    // a gap between two sections.
    layout->addStretch(1);
    auto* footer=new QHBoxLayout;footer->setSpacing(12);
    auto* reset=new QPushButton("Reset session error counts");footer->addWidget(reset);
    footer->addStretch();
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close);
    ui::primary(buttons->button(QDialogButtonBox::Close));
    footer->addWidget(buttons);
    layout->addLayout(footer);
    connect(reset,&QPushButton::clicked,&dialog,[this]{e.health->reset();});
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);

    const auto refresh=[&]{
        auto& health=*e.health;
        const bool live=health.available;
        const bool graph=live&&health.quantum>0;
        statusPill->setText(live?"LIVE":"OFFLINE");
        statusPill->setStyleSheet(live
            ? "color: #141d25; background-color: #6fd3a8; border: 1px solid #6fd3a8;"
            : "color: #e0a340; background-color: transparent; border: 1px solid #3d5262;");
        statusLine->setText(health.status);
        statusDot->setPixmap(nodeMark(live?ui::Tone::Active:ui::Tone::Warn).pixmap(12,12));
        setReadout(figures[0],graph?QString::number(health.rate)+" Hz":QString("—"),graph?ui::Tone::Neutral:ui::Tone::Muted);
        setReadout(figures[1],graph?QString::number(health.quantum):QString("—"),graph?ui::Tone::Neutral:ui::Tone::Muted);
        setReadout(figures[2],graph?QString::number(health.periodMs,'f',2)+" ms":QString("—"),graph?ui::Tone::Neutral:ui::Tone::Muted);
        setReadout(figures[3],graph&&health.load>=0?QString::number(health.load*100,'f',1)+" %":QString("—"),graph&&health.load>=0?ui::Tone::Neutral:ui::Tone::Muted);
        setReadout(figures[4],live?QString::number(health.errors):QString("—"),live&&health.errors>0?ui::Tone::Danger:ui::Tone::Neutral);
        nodesHeader->setText(QString("Nodes · %1").arg(health.nodes.size()).toUpper());
        emptyNote->setGeometry(table->viewport()->rect());
        emptyNote->setVisible(health.nodes.isEmpty());
        table->setRowCount(health.nodes.size());int row=0;
        for(const auto& node:health.nodes){
            const QString load=node.load<0?"—":QString::number(node.load*100,'f',1)+"%";
            const QStringList cells{node.name,load,QString::number(node.sessionErrors),QString::number(node.errors)};
            const ui::Tone state=node.sessionErrors>0?ui::Tone::Danger:nodeLive(node.state)?ui::Tone::Active:ui::Tone::Warn;
            for(int col=0;col<4;++col){
                auto* item=new QTableWidgetItem(cells[col]);
                item->setFont(col?ui::mono(theme::type::value):theme::font(theme::type::label));
                item->setTextAlignment(col?Qt::AlignRight|Qt::AlignVCenter:Qt::AlignLeft|Qt::AlignVCenter);
                if(!col)item->setIcon(nodeMark(state));
                table->setItem(row,col,item);
            }
            ++row;
        }
        int failed=0;for(int i=0;i<8;++i)if(e.plugins->hasFailed(i))++failed;
        rlimit limit{};QString locked="unavailable";if(getrlimit(RLIMIT_MEMLOCK,&limit)==0)locked=limit.rlim_cur==RLIM_INFINITY?"unlimited":QString::number(limit.rlim_cur/1024)+" KiB";
        setReadout(controllerFigures[0],locked,ui::Tone::Neutral);
        setReadout(controllerFigures[1],QString::number(failed),failed>0?ui::Tone::Danger:ui::Tone::Active);
    };
    QTimer timer;timer.setInterval(1000);connect(&timer,&QTimer::timeout,&dialog,refresh);timer.start();refresh();dialog.exec();
}

// Isolated UI regression check; no update request or audio restart is made.
void runHealthUiTest(QApplication& app,Console& console){
    QTimer::singleShot(500,&console,[&app,&console]{
        const QString out=qEnvironmentVariable("LINUX_MIXER_UI_TEST_OUTPUT");if(!out.isEmpty())QDir().mkpath(out);
        auto capture=[out](const QString& title,const QString& filename){
            for(auto* widget:QApplication::topLevelWidgets())if(auto* dialog=qobject_cast<QDialog*>(widget);dialog&&dialog->windowTitle()==title){
                if(!out.isEmpty())dialog->grab().save(out+"/"+filename);
                dialog->accept();return;
            }
            QTextStream(stderr)<<"FAIL: missing dialog "<<title<<'\n';QApplication::exit(1);
        };
        QTimer::singleShot(2000,&console,[capture]{capture("Audio health","audio-health.png");});
        // TEMPORARY CAPTURE SCAFFOLDING: fake graph so the populated state can
        // be photographed on a machine with no PipeWire. DELETE before commit.
        if(!qEnvironmentVariableIsEmpty("LINUX_MIXER_UI_TEST_FAKE")){
            auto& health=*console.e.health;
            health.available=true;health.status="Audio statistics live";health.errors=4;health.quantum=1024;health.rate=48000;health.periodMs=21.3333;health.load=0.114;
            health.nodes={
                {1,1024,48000,0,0,0.021,"pipewire","R",true},
                {2,1024,48000,0,0,0.008,"wireplumber","R",true},
                {3,1024,48000,4,4,0.114,"alsa_output.pci-0000_00_1f.3.analog-stereo","R",true},
                {4,1024,48000,0,0,0.045,"alsa_input.usb-Behringer_UCA202","R",true},
                {5,1024,48000,0,0,0.003,"Firefox","T",false},
                {6,1024,48000,0,0,-1,"mpd","S",false},
                {7,1024,48000,0,0,0.0,"input-alsa-spdif","X",true},
            };
        }
        console.audioHealth();
        QTimer::singleShot(300,&console,[capture]{capture("Voicemixer updates","updates.png");});console.updates->showDialog(&console);
        QTextStream(stdout)<<"PASS: audio health and update dialogs opened and closed without network requests.\n";app.quit();
    });
}
