#!/usr/bin/env python3
"""Check shipped binary catalogs against finished source translations at runtime."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

from pathlib import Path
import json, os, shutil, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
test_root = Path(os.environ.get('DESKPORT_TEST_DIR') or
    (subprocess.check_output(['bash', str(root/'scripts/build-paths.sh'), 'checkout'], text=True).strip() + '/tests'))
test_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='deskport-translations-', dir=test_root) as tmp:
    work = Path(tmp)
    languages = [locale['qt'] for locale in json.loads((root/'shared/deskport-core/i18n/languages.json').read_text()) if locale['qt'] != 'en']
    catalogs = work / 'catalogs'
    catalogs.mkdir()
    existing = os.environ.get('DESKPORT_TRANSLATIONS_DIR')
    qmake = os.environ.get('DESKPORT_QMAKE', 'qmake')
    lrelease = os.environ.get('DESKPORT_LRELEASE') or shutil.which('lrelease')
    if not existing and not lrelease:
        qt_bins = subprocess.check_output([qmake, '-query', 'QT_HOST_BINS'], text=True).strip()
        lrelease = str(Path(qt_bins) / 'lrelease')
    for language in languages:
        source = root / 'app/languages' / f'qml_{language}.ts'
        shutil.copy2(source, catalogs / source.name)
        compiled = catalogs / f'qml_{language}.qm'
        if existing:
            shutil.copy2(Path(existing) / compiled.name, compiled)
        else:
            subprocess.run([lrelease, '-silent', str(source), '-qm', str(compiled)], check=True)
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
    for (int argument = 2; argument < argc; ++argument) {
        const auto language = QString::fromUtf8(argv[argument]);
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
    subprocess.run([qmake,'test.pro'],cwd=work,check=True,stdout=subprocess.DEVNULL)
    subprocess.run(['make','-j4'],cwd=work,check=True,stdout=subprocess.DEVNULL)
    subprocess.run([str(work/'translation-test'), str(catalogs), *languages],check=True)
