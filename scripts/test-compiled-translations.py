#!/usr/bin/env python3
"""Check shipped binary catalogs against finished source translations at runtime."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-translations-') as tmp:
    work = Path(tmp)
    (work/'test.cpp').write_text(r'''
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTranslator>
#include <QXmlStreamReader>
#include <QDebug>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int checked = 0;
    for (const auto& language : {"zh_CN","zh_TW","ja","ko","de","fr","es"}) {
        const auto base = QString::fromLocal8Bit(argv[1])+"/qml_"+language;
        QTranslator translator;
        if (!translator.load(base+".qm")) return 1;
        QFile file(base+".ts"); if (!file.open(QIODevice::ReadOnly)) return 2;
        QXmlStreamReader xml(&file);
        QString context;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement()) continue;
            if (xml.name()==QStringLiteral("name")) context=xml.readElementText();
            if (xml.name()!=QStringLiteral("message")) continue;
            bool skip=xml.attributes().value("numerus")==QStringLiteral("yes");
            QString source, translation, comment;
            while (xml.readNextStartElement()) {
                if (xml.name()==QStringLiteral("source")) source=xml.readElementText();
                else if(xml.name()==QStringLiteral("comment")) comment=xml.readElementText();
                else if(xml.name()==QStringLiteral("translation")) {
                    skip |= !xml.attributes().value("type").isEmpty();
                    translation=xml.readElementText(QXmlStreamReader::IncludeChildElements);
                } else xml.skipCurrentElement();
            }
            if(skip || translation.isEmpty()) continue;
            const auto actual=translator.translate(context.toUtf8(),source.toUtf8(),comment.isEmpty()?nullptr:comment.toUtf8().constData());
            if(actual!=translation) { qCritical()<<language<<context<<source<<"stale compiled translation"; return 3; }
            ++checked;
        }
        if(xml.hasError()) return 4;
    }
    qInfo()<<"PASS:"<<checked<<"shipped translations match source catalogs at runtime";
}
''')
    (work/'test.pro').write_text('QT = core\nCONFIG += console c++17\nCONFIG -= app_bundle\nTARGET = translation-test\nSOURCES = test.cpp\n')
    subprocess.run([os.environ.get('DESKPORT_QMAKE','qmake'),'test.pro'],cwd=work,check=True,stdout=subprocess.DEVNULL)
    subprocess.run(['make','-j4'],cwd=work,check=True,stdout=subprocess.DEVNULL)
    subprocess.run([str(work/'translation-test'),str(root/'app/languages')],check=True)
