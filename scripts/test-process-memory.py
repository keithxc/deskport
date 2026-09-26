#!/usr/bin/env python3
"""Exercise native memory sampling with a real child and an exited process."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-memory-sample-') as tmp:
    work = Path(tmp)
    (work/'test.cpp').write_text(r'''
#include "processmemory.h"
#include <QProcess>
#include <QThread>
#include <QDebug>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--child")) { QThread::sleep(20); return 0; }
    if (ProcessMemory::resident(0) != -1 || ProcessMemory::resident(-1) != -1) return 1;
    const auto initial = ProcessMemory::sample(0);
    if (!initial["available"].toBool() || initial["client"].toLongLong() <= 0) return 2;
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {"--child"});
    if (!child.waitForStarted()) return 3;
    QThread::msleep(200);
    const auto pid = child.processId();
    const auto sample = ProcessMemory::sample(pid);
    if (!sample["complete"].toBool() || sample["host"].toLongLong() <= 0 || sample["processes"].toInt() != 2) return 4;
    if (sample["total"].toLongLong() != sample["client"].toLongLong() + sample["host"].toLongLong() + sample["helpers"].toLongLong()) return 5;
    child.terminate(); if (!child.waitForFinished(5000)) { child.kill(); child.waitForFinished(); }
    const auto exited = ProcessMemory::sample(pid);
    if (exited["complete"].toBool() || exited["host"].toLongLong() != -1) return 6;
    qInfo() << "PASS native current/child/exited process samples and deduplication";
}
''')
    (work/'test.pro').write_text('QT = core\nCONFIG += console c++17\nCONFIG -= app_bundle\nTARGET = memory-test\nSOURCES = test.cpp\nINCLUDEPATH += '+str(root/'app/backend')+'\nwin32:LIBS += -lpsapi\n')
    subprocess.run([os.environ.get('DESKPORT_QMAKE','qmake'),'test.pro'],cwd=work,check=True,stdout=subprocess.DEVNULL)
    subprocess.run(['make','-j4'],cwd=work,check=True,stdout=subprocess.DEVNULL)
    subprocess.run([str(work/'memory-test')],check=True)
