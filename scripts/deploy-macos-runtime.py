#!/usr/bin/env python3
"""Deploy only the QML import closure and the required Qt plugin categories."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys

app, source = map(Path, sys.argv[1:3])
def query(key):
    return Path(subprocess.check_output(['qmake', '-query', key], text=True).strip())
qml = Path(os.environ['DESKPORT_QML_IMPORT_PATH']) if os.environ.get('DESKPORT_QML_IMPORT_PATH') else query('QT_INSTALL_QML')
plugin_paths = [Path(p) for p in os.environ.get('DESKPORT_QT_PLUGIN_PATH', '').split(os.pathsep) if p]
if not plugin_paths:
    plugin_paths = [query('QT_INSTALL_PLUGINS')]
scanner = Path(os.environ['DESKPORT_QML_SCANNER']) if os.environ.get('DESKPORT_QML_SCANNER') else query('QT_INSTALL_LIBEXECS') / 'qmlimportscanner'
def copy_runtime_tree(src, dest, **options):
    shutil.copytree(src, dest, dirs_exist_ok=True, **options)
    # Nix store directories are read-only. Later imports can populate a child
    # module, and signing needs writable files in this disposable staging tree.
    for path in [dest, *dest.rglob('*')]:
        path.chmod(path.stat().st_mode | 0o200)

imports = json.loads(subprocess.check_output([str(scanner), '-rootPath', str(source), '-importPath', str(qml)], text=True))
for module in imports:
    if not module.get('path') or not module.get('relativePath'):
        continue
    src = Path(module['path'])
    dest = app / 'Contents/Resources/qml' / module['relativePath']
    # A parent module directory can contain many unrelated child modules.
    def ignore(directory, names):
        return [name for name in names if (Path(directory) / name / 'qmldir').is_file()]
    copy_runtime_tree(src, dest, ignore=ignore)
for plugins in plugin_paths:
    for name in ['platforms', 'imageformats', 'iconengines', 'styles', 'tls', 'networkinformation']:
        if (plugins / name).exists():
            copy_runtime_tree(plugins / name, app / 'Contents/PlugIns' / name)
(app / 'Contents/Resources/qt.conf').write_text('[Paths]\nPlugins=PlugIns\nQmlImports=Resources/qml\n')
print('Deployed QML import closure and Qt plugins')
