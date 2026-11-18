#!/usr/bin/env python3
"""Exercise SSH setup with isolated homes and fake systemd/desktop tools."""

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
import socket
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix="deskport-setup-") as temporary:
    work = Path(temporary)
    (work / "main.cpp").write_text('''#include <QCoreApplication>
#include "cli/hostsetup.h"
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    return DeskPortCli::runSetupCommand(application.arguments());
}
''')
    (work / "test.pro").write_text(f'''QT = core
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = setup-driver
SOURCES += "{work}/main.cpp" "{root}/app/cli/hostsetup.cpp"
INCLUDEPATH += "{root}/app"
''')
    subprocess.run([os.environ.get("DESKPORT_QMAKE", "qmake"), "test.pro"], cwd=work, check=True)
    subprocess.run(["make", "-j4"], cwd=work, check=True, stdout=subprocess.DEVNULL)
    binary_dir = work / 'prefix %$"' / "bin"
    binary_dir.mkdir(parents=True)
    binary = binary_dir / "deskport"
    shutil.copy2(work / "setup-driver", binary)
    helper_dir = binary_dir.parent / "libexec"
    helper_dir.mkdir()
    for name in ("deskport-host", "deskport-display"):
        helper = helper_dir / name
        helper.write_text("#!/bin/sh\nexit 0\n")
        helper.chmod(0o755)
    fakebin = work / "fakebin"
    fakebin.mkdir()
    events = work / "systemctl-events.jsonl"
    systemctl = fakebin / "systemctl"
    systemctl.write_text(f'''#!{sys.executable}
import json, os, sys
with open(os.environ["SETUP_EVENTS"], "a") as events:
    events.write(json.dumps(sys.argv[1:]) + "\\n")
if os.environ.get("SETUP_MANAGER_FAIL"):
    sys.exit(1)
args = sys.argv[1:]
if "daemon-reload" in args:
    sys.exit(1 if os.environ.get("SETUP_RELOAD_FAIL") else 0)
if "--property=FragmentPath" in args:
    print(os.environ.get("SETUP_FRAGMENT", ""))
elif "--property=DropInPaths" in args:
    print(os.environ.get("SETUP_DROPINS", ""))
elif "--property=Version" in args:
    print("test-version")
elif "--value" in args and "--property=ActiveState" in args:
    print(os.environ.get("SETUP_ACTIVE", "inactive"))
elif "--value" in args and "--property=UnitFileState" in args:
    print(os.environ.get("SETUP_ENABLED", "disabled"))
else:
    print("LoadState=loaded\\nActiveState=inactive\\nSubState=dead\\nUnitFileState=disabled")
''')
    systemctl.chmod(0o755)
    for name, output in (("dbus-send", ""), ("hyprctl", '[{"name":"PRIVATE-MONITOR-SENTINEL"}]')):
        tool = fakebin / name
        tool.write_text(f"#!{sys.executable}\nprint({output!r})\n")
        tool.chmod(0o755)
    config = work / "config"
    runtime = work / "runtime"
    runtime.mkdir(mode=0o700)
    env = dict(os.environ, HOME=str(work / "home"), XDG_CONFIG_HOME=str(config),
               XDG_DATA_HOME=str(work / "data"), XDG_CACHE_HOME=str(work / "cache"),
               XDG_RUNTIME_DIR=str(runtime), DBUS_SESSION_BUS_ADDRESS="unix:path=/fixture-only-do-not-connect",
               PATH=str(fakebin), SETUP_EVENTS=str(events), SECRET_TEST_VALUE="PRIVATE-ENV-SENTINEL")
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "XDG_CURRENT_DESKTOP", "HYPRLAND_INSTANCE_SIGNATURE", "APPIMAGE", "APPDIR"):
        env.pop(key, None)

    count = 0

    def invoke(*args, code=0, extra=None):
        global count
        result = subprocess.run([str(binary), *args], env=dict(env, **(extra or {})),
                                capture_output=True, text=True, timeout=20)
        assert result.returncode == code, (args, result.returncode, result.stdout, result.stderr)
        assert "PRIVATE-ENV-SENTINEL" not in result.stdout + result.stderr
        assert "PRIVATE-MONITOR-SENTINEL" not in result.stdout + result.stderr
        count += 1
        return result

    invoke("service", "--help")
    invoke("service", "install", "--help")
    invoke("doctor", "--typo", code=2)
    if not sys.platform.startswith("linux"):
        assert json.loads(invoke("doctor", "--json", code=3).stdout)["status"] == "unsupported"
        invoke("service", "install", code=3)
        assert not events.exists()
        print(f"{count} non-Linux setup checks passed; Linux service cases require a native Linux run.")
        sys.exit(0)

    report = json.loads(invoke("doctor", "--json", code=3).stdout)
    assert report["schemaVersion"] == 1 and report["status"] == "unsupported"
    assert {c["id"]: c["status"] for c in report["checks"]}["session"] == "unsupported"
    for socket_name in ("wayland-test", "pipewire-0", "bus"):
        probe = socket.socket(socket.AF_UNIX)
        probe.bind(str(runtime / socket_name))
        probe.close()
    report = json.loads(invoke("doctor", "--json", code=1, extra={
        "WAYLAND_DISPLAY": "wayland-test", "XDG_CURRENT_DESKTOP": "Hyprland",
        "HYPRLAND_INSTANCE_SIGNATURE": "PRIVATE-ENV-SENTINEL"}).stdout)
    checks = {c["id"]: c["status"] for c in report["checks"]}
    assert report["status"] == "degraded" and report["compositor"] == "hyprland"
    assert all(checks[key] == "ok" for key in ("session", "runtime", "hyprland", "pipewire", "session_bus"))
    assert checks["capture_permission"] == "degraded" and checks["encoder"] == "degraded"
    unit = config / "systemd/user/io.github.keithxc.DeskPort.service"
    invoke("service", "install", "--typo", code=2)
    invoke("service", "install", extra={"SETUP_MANAGER_FAIL": "1"}, code=1)
    assert not unit.exists()
    invoke("service", "install", extra={"SETUP_FRAGMENT": "/usr/lib/systemd/user/foreign.service"}, code=1)
    assert not unit.exists()
    invoke("service", "install", extra={"SETUP_DROPINS": "/etc/systemd/user/manual.conf"}, code=1)
    assert not unit.exists()
    invoke("service", "install")
    first = unit.read_text()
    assert '--background --share' in first and 'WantedBy=graphical-session.target' in first
    assert 'KillMode=mixed' in first and 'KillMode=control-group' not in first
    assert '%%$$\\"/bin/deskport"' in first, first
    first_mtime = unit.stat().st_mtime_ns
    invoke("service", "install")
    assert unit.stat().st_mtime_ns == first_mtime
    invoke("service", "install", "--headless")
    assert 'host run' in unit.read_text() and 'WantedBy=default.target' in unit.read_text()
    assert 'graphical-session' not in unit.read_text()
    state = json.loads(invoke("service", "status", "--json").stdout)
    assert state["managedByCli"] and state["ActiveState"] == "inactive"
    invoke("service", "uninstall", code=1, extra={"SETUP_ACTIVE": "active"})
    invoke("service", "uninstall", code=1, extra={"SETUP_ENABLED": "enabled"})
    saved = unit.read_text()
    unit.write_text(saved + "# User customization\n")
    invoke("service", "install", code=1)
    invoke("service", "uninstall", code=1)
    assert unit.read_text().endswith("# User customization\n")
    unit.write_text(saved)
    drop_ins = unit.parent / (unit.name + ".d")
    drop_ins.mkdir()
    (drop_ins / "manual.conf").write_text("[Service]\nRestart=no\n")
    invoke("service", "install", code=1)
    invoke("service", "uninstall", code=1)
    shutil.rmtree(drop_ins)
    invoke("service", "uninstall")
    assert not unit.exists()
    invoke("service", "uninstall")
    unit.symlink_to("/nix/store/missing-fixture/user.service")
    invoke("service", "install", code=1)
    invoke("service", "uninstall", code=1)
    assert unit.is_symlink()
    unit.unlink()
    unit.symlink_to(work / "foreign-unit")
    invoke("service", "install", code=1)
    assert unit.is_symlink()
    unit.unlink()
    unit.write_text("[Service]\nExecStart=/manual/deskport\n")
    invoke("service", "install", code=1)
    invoke("service", "uninstall", code=1)
    assert "ExecStart=/manual/deskport" in unit.read_text()
    unit.unlink()
    invoke("service", "install", code=1, extra={"SETUP_RELOAD_FAIL": "1"})
    assert unit.exists()
    invoke("service", "install")  # Retry reload even when the file did not change.
    wrapped = binary_dir / ".deskport-wrapped"
    shutil.copy2(binary, wrapped)
    launcher = binary
    binary = wrapped
    invoke("service", "install")
    assert '.deskport-wrapped' not in unit.read_text(), unit.read_text()
    launcher.rename(binary_dir / "launcher-saved")
    invoke("service", "install", code=1)
    (binary_dir / "launcher-saved").rename(launcher)
    extracted = work / "extracted AppImage"
    (extracted / "usr/shared/bin").mkdir(parents=True)
    binary = extracted / "usr/shared/bin/deskport"
    shutil.copy2(work / "setup-driver", binary)
    app_run = extracted / "AppRun"
    app_run.write_text("#!/bin/sh\nexit 0\n")
    app_run.chmod(0o755)
    invoke("service", "install", extra={"APPDIR": str(extracted)})
    assert f'ExecStart="{app_run.resolve()}"' in unit.read_text(), unit.read_text()
    history = [json.loads(line) for line in events.read_text().splitlines()]
    assert all(not set(command) & {"start", "stop", "enable", "disable", "restart", "--now", "show-environment"}
               for command in history), history
    assert ["--user", "daemon-reload"] in history[-4:]
    print(f"{count} Linux setup checks passed without a real service manager, compositor, input device, or personal configuration.")
