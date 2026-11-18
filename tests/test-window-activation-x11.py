#!/usr/bin/env python3
"""Exercise Qt's X11 grab focus state on a private Xvfb without sending input."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()


import argparse
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xvfb", default=os.environ.get("DESKPORT_XVFB") or shutil.which("Xvfb"))
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        parser.error("This fixture requires Linux X11")
    if not args.xvfb:
        parser.error("Xvfb is required; use --xvfb or DESKPORT_XVFB")
    with tempfile.TemporaryDirectory(prefix="deskport-x11-activation-") as temporary:
        work = Path(temporary)
        project = "\n".join([
            "QT += core gui", "CONFIG += console c++17 link_pkgconfig", "CONFIG -= app_bundle",
            "DEFINES += HAS_X11", "PKGCONFIG += sdl2 x11", "TARGET = window-activation-x11",
            f'INCLUDEPATH += "{ROOT}/app"',
            f'SOURCES += "{ROOT}/tests/window-activation-x11.cpp" "{ROOT}/app/streaming/windowactivation.cpp" "{ROOT}/app/streaming/canvasbridge.cpp"',
        ])
        (work / "test.pro").write_text(project + "\n")
        qmake = os.environ.get("DESKPORT_QMAKE", "qmake")
        subprocess.run([qmake, "test.pro"], cwd=work, check=True)
        jobs = str(max(1, min(4, int(os.environ.get("JOBS", "2")))))
        subprocess.run(["make", "-j" + jobs], cwd=work, check=True, stdout=subprocess.DEVNULL)
        env = dict(os.environ, QT_QPA_PLATFORM="xcb", QT_QPA_PLATFORMTHEME="generic",
                   QT_STYLE_OVERRIDE="Fusion", DESKPORT_TEST_PRIVATE_X11="1",
                   DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent-deskport-private-bus")
        env["QT_PLUGIN_PATH"] = subprocess.check_output([qmake, "-query", "QT_INSTALL_PLUGINS"], text=True).strip()
        for key in ("DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "XDG_ACTIVATION_TOKEN", "SESSION_MANAGER"):
            env.pop(key, None)
        for key in ("HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"):
            path = work / key.lower()
            path.mkdir(mode=0o700)
            env[key] = str(path)
        read_fd, write_fd = os.pipe()
        with (work / "xvfb.log").open("w+") as log:
            server = None
            try:
                server = subprocess.Popen([args.xvfb, "-displayfd", str(write_fd), "-screen", "0",
                                           "800x600x24", "-nolisten", "tcp", "-ac"],
                                          pass_fds=[write_fd], stdout=log, stderr=subprocess.STDOUT, env=env)
                os.close(write_fd)
                write_fd = None
                if not select.select([read_fd], [], [], 10)[0]:
                    raise RuntimeError("Private Xvfb did not provide a display")
                number = os.read(read_fd, 32).decode().strip()
                if not number.isdigit() or server.poll() is not None:
                    raise RuntimeError("Private Xvfb failed to start")
                env["DISPLAY"] = ":" + number
                subprocess.run([str(work / "window-activation-x11")], cwd=work, env=env,
                               check=True, timeout=30)
            finally:
                os.close(read_fd)
                if write_fd is not None:
                    os.close(write_fd)
                if server is not None and server.poll() is None:
                    server.terminate()
                    try:
                        server.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        server.kill()
                        server.wait()


if __name__ == "__main__":
    main()
