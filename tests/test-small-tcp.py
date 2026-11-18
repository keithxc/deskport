#!/usr/bin/env python3
"""Compile production TCP/HTTP code and exercise isolated TLS/MSS fixtures."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-small-tcp-') as temporary:
    work = Path(temporary)
    env = dict(os.environ, XDG_CONFIG_HOME=str(work / 'config'), TEST_PYTHON=shutil.which('python3'), TEST_FIXTURE=str(root / 'tests/tcp-fixture.py'))
    for name in ('test', 'other'):
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2', '-subj', '/CN=localhost', '-keyout', str(work / f'{name}.key'), '-out', str(work / f'{name}.pem')], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    env.update(TEST_CERT=str(work / 'test.pem'), TEST_KEY=str(work / 'test.key'), TEST_OTHER_CERT=str(work / 'other.pem'))
    (work / 'test.pro').write_text(f'''QT += core gui network testlib
CONFIG += console c++17 testcase link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += openssl
TARGET = small-tcp-tests
SOURCES += "{root}/tests/small-tcp.cpp" "{root}/app/backend/nvhttp.cpp" "{root}/app/backend/nvaddress.cpp" "{root}/app/backend/nvapp.cpp" "{root}/app/backend/identitymanager.cpp"
HEADERS += "{root}/app/backend/nvhttp.h"
INCLUDEPATH += "{root}/app/backend" "{root}/app" "{root}/moonlight-common-c/moonlight-common-c/src"
''')
    subprocess.run([os.environ.get('DESKPORT_QMAKE', 'qmake'), 'test.pro'], cwd=work, env=env, check=True)
    subprocess.run(['make', '-j4'], cwd=work, env=env, check=True)
    subprocess.run([str(work / 'small-tcp-tests')], cwd=work, env=env, check=True)
