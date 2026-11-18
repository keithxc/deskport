#!/usr/bin/env python3
"""Test production per-device persistence without reading user settings."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
test_root = Path(os.environ.get('DESKPORT_TEST_DIR') or
    (subprocess.check_output(['bash', str(root/'scripts/build-paths.sh'), 'checkout'], text=True).strip() + '/tests'))
test_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="deskport-device-preferences-", dir=test_root) as temporary:
    work = Path(temporary)
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
        compiled = catalogs / f'qml_{language}.qm'
        if existing:
            shutil.copy2(Path(existing) / compiled.name, compiled)
        else:
            subprocess.run([lrelease, '-silent', str(source), '-qm', str(compiled)], check=True)
    (work / "test.pro").write_text(f'''QT += core gui qml network testlib
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TARGET = device-preferences
SOURCES += "{root}/tests/device-preferences.cpp" "{root}/app/settings/streamingpreferences.cpp"
HEADERS += "{root}/app/settings/streamingpreferences.h"
RESOURCES += test-resources.qrc
INCLUDEPATH += "{root}/app"
''')
    resources = ['<RCC><qresource prefix="/">',
        f'<file alias="i18n/languages.json">{root}/shared/deskport-core/i18n/languages.json</file>']
    resources += [f'<file alias="languages/qml_{language}.qm">{catalogs}/qml_{language}.qm</file>'
                  for language in languages]
    (work/'test-resources.qrc').write_text('\n'.join(resources+['</qresource></RCC>']))
    # QSettings can flush after an inner QTemporaryDir has been removed. Keep
    # every child-process temporary path inside the outer cleanup boundary.
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", XDG_CONFIG_HOME=temporary,
               TMPDIR=temporary, TMP=temporary, TEMP=temporary)
    env.setdefault("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer")
    subprocess.run([qmake, "test.pro"], cwd=work, env=env, check=True)
    subprocess.run(["make", "-j4"], cwd=work, env=env, check=True, stdout=subprocess.DEVNULL)
    subprocess.run([str(work / "device-preferences")], cwd=work, env=env, check=True)
