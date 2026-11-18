#!/usr/bin/env python3
"""Check a relocated AppDir under Xvfb without a real desktop or personal state."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

root = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='deskport-runtime-') as directory:
    state = Path(directory)
    env = {'PATH': '/usr/bin:/bin', 'HOME': directory,
           'DISPLAY': os.environ['DISPLAY'], 'QT_QPA_PLATFORM': 'xcb',
           'SDL_AUDIODRIVER': 'dummy', 'QT_LOGGING_TO_CONSOLE': '1',
           'DESKPORT_CLIENT_LOG': str(state / 'client.log')}
    for variable in ('XDG_CONFIG_HOME', 'XDG_CACHE_HOME', 'XDG_DATA_HOME', 'XDG_RUNTIME_DIR'):
        path = state / variable.lower()
        path.mkdir(mode=0o700)
        env[variable] = str(path)
    if 'XAUTHORITY' in os.environ:
        env['XAUTHORITY'] = os.environ['XAUTHORITY']
    # Exercise private libraries under real X11, without disabling OpenGL/Qt RHI.
    with (state / 'viewer.log').open('w+') as log:
        child = subprocess.Popen([str(root / 'AppRun'), '--no-host-autostart'],
                                 env=env, cwd=state, stdout=log, stderr=log,
                                 start_new_session=True)
        try:
            time.sleep(10)
            log.flush()
            assert child.poll() is None, (child.returncode, (state / 'viewer.log').read_text(),
                (state / 'client.log').read_text() if (state / 'client.log').exists() else '')
            executable = Path(f'/proc/{child.pid}/exe').resolve()
            assert executable == root / 'usr/bin/deskport', executable
            maps = Path(f'/proc/{child.pid}/maps').read_text()
            assert str(root / 'usr/shared/lib/libc.so.6') in maps, maps
            assert str(root / 'usr/shared/lib/ld-linux-x86-64.so.2') in maps, maps
            windows = subprocess.check_output(['xwininfo', '-root', '-tree'], env=env, text=True, timeout=10)
            assert 'DeskPort' in windows, windows
            external = sorted({line.split()[-1] for line in maps.splitlines()
                               if '.so' in line and line.split()[-1].startswith('/')
                               and not line.split()[-1].startswith(str(root) + '/')})
            print('External library mappings:', external)
            # Hardware drivers may come from the host; the normal GUI runtime
            # must come entirely from the bundle in this software-only check.
            assert not external, external
        finally:
            if child.poll() is None:
                os.killpg(child.pid, signal.SIGTERM)
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.wait()
        log.seek(0)
        output = log.read()
        for failure in ('QQmlApplicationEngine failed', 'is not installed',
                        'Cannot load library', 'Failed to create RHI',
                        'Failed to create OpenGL context'):
            assert failure not in output, output
print('PASS: X11 window, private libc/loader, relocated executable identity, no external GUI libraries')
