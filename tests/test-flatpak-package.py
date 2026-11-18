#!/usr/bin/env python3
"""Validate an installed Flatpak using a private session bus and sandbox state.

Install the bundle into a separate FLATPAK_USER_DIR first. Uses offscreen rendering
and disables sandbox networking. Only this test's process groups are terminated.
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
import signal
import subprocess
import sys
import tempfile
import time

if '--isolated' not in sys.argv:
    subprocess.run(['dbus-run-session', '--', sys.executable, __file__, sys.argv[1], '--isolated'], check=True)
    raise SystemExit(0)

version = sys.argv[1]
base = ['flatpak', 'run', '--unshare=network', '--env=QT_QPA_PLATFORM=offscreen',
        '--env=QT_QUICK_BACKEND=software', '--env=SDL_VIDEODRIVER=dummy',
        '--command=sh', 'io.github.keithxc.DeskPort', '-c']
# Flatpak resets reserved XDG variables during startup, so set them inside.
setup = 'export XDG_CONFIG_HOME=/tmp/deskport-test-config XDG_DATA_HOME=/tmp/deskport-test-data XDG_CACHE_HOME=/tmp/deskport-test-cache; '
result = subprocess.run(base + [setup + 'exec /app/bin/deskport --version'],
                        capture_output=True, text=True, timeout=30, check=True)
assert version in result.stdout + result.stderr
command = base + [setup + 'exec /app/bin/deskport --no-host-autostart']
with tempfile.TemporaryDirectory(prefix='deskport-flatpak-test-') as temporary:
    for cycle in range(3):
        with (pathlib.Path(temporary) / f'gui-{cycle}.log').open('w+') as log:
            child = subprocess.Popen(command, stdout=log, stderr=log, start_new_session=True)
            try:
                time.sleep(6)
                assert child.poll() is None, 'Owner exited before GUI validation'
                duplicate = subprocess.run(command, capture_output=True, text=True, timeout=10)
                assert duplicate.returncode == 0, duplicate.stdout + duplicate.stderr
                assert child.poll() is None, 'Duplicate terminated owner'
            finally:
                if child.poll() is None:
                    os.killpg(child.pid, signal.SIGKILL)
                child.wait(timeout=10)
            log.seek(0)
            output = log.read()
            for failure in ['QQmlApplicationEngine failed', 'is not installed', 'Cannot load library', 'already running']:
                assert failure not in output, output
            time.sleep(1)
print('PASS: Flatpak version, isolated QML GUI, duplicate launch and three crash/restart cycles')
