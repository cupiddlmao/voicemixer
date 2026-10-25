#include "console.h"
#include "audio_health.h"
#include "update_checker.h"
#include <QtWidgets/QApplication>
#include <QtCore/QDir>
#include <QtCore/QTextStream>
#include "plugin_racks.h"
#include <QtWidgets/QDialog>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QDialogButtonBox>
#include <QtCore/QTimer>
#include <sys/resource.h>
void Console::audioHealth(){
    QDialog dialog(this);dialog.setWindowTitle("Audio health");dialog.resize(790,530);styleMixerDialog(dialog);
    auto* layout=new QVBoxLayout(&dialog);auto* summary=new QLabel;summary->setWordWrap(true);layout->addWidget(summary);
    auto* table=new QTableWidget;table->setColumnCount(4);table->setHorizontalHeaderLabels({"Node","Load","Errors since reset","Node counter"});
    table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);table->horizontalHeader()->setSectionResizeMode(3,QHeaderView::ResizeToContents);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->verticalHeader()->hide();layout->addWidget(table);
    auto* details=new QLabel;details->setWordWrap(true);layout->addWidget(details);
    auto* explanation=new QLabel("Load is the fraction of an audio processing period used, not total CPU usage. Errors count PipeWire node errors after the first sample; one interruption can affect several nodes. Buffer time is one graph period, not end-to-end latency. The node counter can restart when a device restarts; session totals keep accumulated errors. Statistics are read continuously; the first two seconds establish a baseline.");explanation->setWordWrap(true);layout->addWidget(explanation);
    auto* reset=new QPushButton("Reset session error counts");layout->addWidget(reset);connect(reset,&QPushButton::clicked,&dialog,[this]{e.health->reset();});
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    const auto refresh=[&]{
        auto& health=*e.health;
        QString text=health.status;
        if(health.available){text+=QString("\n%1 node errors since reset").arg(health.errors);if(health.quantum>0)text+=QString(" · %1 frames / %2 Hz · %3 ms per period · busiest driver %4%").arg(health.quantum).arg(health.rate).arg(health.periodMs,0,'f',2).arg(health.load*100,0,'f',1);}
        summary->setText(text);table->setRowCount(health.nodes.size());int row=0;
        for(const auto& node:health.nodes){const QStringList cells{node.name,node.load<0?"—":QString::number(node.load*100,'f',1)+"%",QString::number(node.sessionErrors),QString::number(node.errors)};for(int col=0;col<4;++col)table->setItem(row,col,new QTableWidgetItem(cells[col]));++row;}
        int failed=0;for(int i=0;i<8;++i)if(e.plugins->hasFailed(i))++failed;
        rlimit limit{};QString locked="unavailable";if(getrlimit(RLIMIT_MEMLOCK,&limit)==0)locked=limit.rlim_cur==RLIM_INFINITY?"unlimited":QString::number(limit.rlim_cur/1024)+" KiB";
        details->setText(QString("Controller memory-lock limit: %1 · Failed plugin racks: %2\nHung commands time out automatically. Unresponsive plugin racks restore normal input routing; click Plugins on that input to retry.").arg(locked).arg(failed));
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
        QTimer::singleShot(2000,&console,[capture]{capture("Audio health","audio-health.png");});console.audioHealth();
        QTimer::singleShot(300,&console,[capture]{capture("Voicemixer updates","updates.png");});console.updates->showDialog(&console);
        QTextStream(stdout)<<"PASS: audio health and update dialogs opened and closed without network requests.\n";app.quit();
    });
}
