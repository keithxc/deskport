#!/usr/bin/env python3
"""Test Flatpak's session-bus ownership, activation and crash recovery in isolation.

Requires a C++ compiler, pkg-config, Qt 6 Core/Network/DBus development files and
dbus-run-session. No desktop, network or existing session bus is used.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import os
import pathlib
import select
import shlex
import subprocess
import sys
import tempfile

if len(sys.argv) == 1:
    subprocess.run(['dbus-run-session', '--', sys.executable, __file__, '--isolated'], check=True)
    raise SystemExit(0)

source = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-instance-test-') as temporary:
    work = pathlib.Path(temporary)
    (work / 'test.cpp').write_text(r'''
#include <QCoreApplication>
#include <cstdio>
#include "singleinstance.h"
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    SingleInstance instance;
    instance.activate = [] { puts("activated"); fflush(stdout); };
    if (!instance.start({}, argc < 2 || QString(argv[1]) != "ping"))
        return instance.delivered ? 0 : 2;
    puts("owner"); fflush(stdout);
    return app.exec();
}
''')
    flags = shlex.split(subprocess.check_output(
        ['pkg-config', '--cflags', '--libs', 'Qt6Core', 'Qt6Network', 'Qt6DBus'], text=True))
    binary = str(work / 'test')
    subprocess.run(['c++', '-std=c++17', '-fPIC', '-I' + str(source / 'app/backend'),
                    str(work / 'test.cpp'), '-o', binary] + flags, check=True)
    env = dict(os.environ, FLATPAK_ID='io.github.keithxc.DeskPort', HOME=temporary)

    def line(child):
        assert select.select([child.stdout], [], [], 5)[0], 'No ownership/activation response'
        return child.stdout.readline().strip()

    for cycle in range(3):
        owner = subprocess.Popen([binary], env=env, stdout=subprocess.PIPE, text=True)
        try:
            assert line(owner) == 'owner'
            subprocess.run([binary, 'ping'], env=env, check=True, timeout=5)
            assert not select.select([owner.stdout], [], [], 0.2)[0], 'Ping activated GUI'
            subprocess.run([binary], env=env, check=True, timeout=5)
            assert line(owner) == 'activated'
        finally:
            owner.kill()
            owner.wait(timeout=5)
    print('PASS: exclusive ownership, activation, non-activating ping, three crash/restart cycles')
