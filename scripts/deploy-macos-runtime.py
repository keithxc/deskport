#!/usr/bin/env python3
"""Deploy only the QML import closure and the required Qt plugin categories."""
from pathlib import Path
import json
import shutil
import subprocess
import sys

app, source = map(Path, sys.argv[1:3])
def query(key):
    return Path(subprocess.check_output(['qmake', '-query', key], text=True).strip())
qml = query('QT_INSTALL_QML')
plugins = query('QT_INSTALL_PLUGINS')
scanner = query('QT_INSTALL_LIBEXECS') / 'qmlimportscanner'
imports = json.loads(subprocess.check_output([str(scanner), '-rootPath', str(source), '-importPath', str(qml)], text=True))
for module in imports:
    if not module.get('path') or not module.get('relativePath'):
        continue
    src = Path(module['path'])
    dest = app / 'Contents/Resources/qml' / module['relativePath']
    # A parent module directory can contain many unrelated child modules.
    def ignore(directory, names):
        return [name for name in names if (Path(directory) / name / 'qmldir').is_file()]
    shutil.copytree(src, dest, dirs_exist_ok=True, ignore=ignore)
for name in ['platforms', 'imageformats', 'iconengines', 'styles', 'tls', 'networkinformation']:
    if (plugins / name).exists():
        shutil.copytree(plugins / name, app / 'Contents/PlugIns' / name, dirs_exist_ok=True)
(app / 'Contents/Resources/qt.conf').write_text('[Paths]\nPlugins=PlugIns\nQmlImports=Resources/qml\n')
print('Deployed QML import closure and Qt plugins')
