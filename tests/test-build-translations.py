#!/usr/bin/env python3
"""Exercise the production qmake translation block without compiling the app."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
qmake = os.environ.get('DESKPORT_QMAKE') or shutil.which('qmake') or shutil.which('qmake6')
if not qmake:
    raise SystemExit('Set DESKPORT_QMAKE or run this check in nix develop')
base = Path(os.environ.get('DESKPORT_TEST_DIR') or
    (subprocess.check_output(['bash', str(root / 'scripts/build-paths.sh'), 'checkout'], text=True).strip() + '/tests'))
base.mkdir(parents=True, exist_ok=True)
production = (root / 'app/app.pro').read_text()
block = production[production.index('# Compile catalogs into the build tree'):production.index('# Additional import path used')]
with tempfile.TemporaryDirectory(prefix='translations-', dir=base) as temporary:
    directory = Path(temporary)
    source = directory / 'source with spaces'
    output = directory / 'work with spaces'
    (source / 'languages').mkdir(parents=True)
    output.mkdir()
    for locale in ('de', 'pt_BR'):
        (source / 'languages' / f'qml_{locale}.ts').write_text(
            '<?xml version="1.0"?><TS version="2.1" language="de"><context>'
            '<name>Test</name><message><source>Hello</source><translation>Hallo</translation>'
            '</message></context></TS>')
    (source / 'test.pro').write_text('TEMPLATE = aux\nCONFIG -= qt\nload(qt_functions)\n'
        'TRANSLATIONS = languages/qml_de.ts languages/qml_pt_BR.ts\n' + block +
        '\nload(resources_functions)\nqtFlattenResources()\n')
    before = {p.relative_to(source): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in source.rglob('*') if p.is_file()}
    environment = dict(os.environ)
    environment.setdefault('DEVELOPER_DIR', '/Applications/Xcode.app/Contents/Developer')
    subprocess.run([qmake, '-early', 'QMAKE_TARGET_BUNDLE_PREFIX=org.example.DeskPortBuildTest',
                    str(source / 'test.pro')],
                   cwd=output, env=environment, check=True)
    after = {p.relative_to(source): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in source.rglob('*') if p.is_file()}
    assert before == after, 'qmake wrote into the source snapshot'
    assert (output / 'translations/qml_de.qm').is_file()
    assert (output / 'translations/qml_pt_BR.qm').is_file()
    resource = ET.parse(output / 'qmake_deskport_translations.qrc').getroot().find('qresource')
    assert resource.get('prefix') == '/languages'
    assert [item.get('alias') for item in resource] == ['qml_de.qm']
    assert Path(resource[0].text) == output / 'translations/qml_de.qm'
    assert not any(item.text.endswith('.qm') for item in ET.parse(root / 'app/resources.qrc').iter('file'))
print('PASS: qmake writes catalogs and qrc only to the build tree; resource aliases and pt_BR exclusion are preserved')
