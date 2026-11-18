#!/usr/bin/env python3
"""Check the built Linux identity without a real desktop or remote host."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()


import configparser
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    expected_version = (Path(__file__).resolve().parents[1] / "app/version.txt").read_text().strip()
    assert expected_version, "Missing application version"
    output = Path(sys.argv[1] if len(sys.argv) > 1 else "result").resolve()
    binary = output / "bin/deskport"
    assert binary.is_file(), f"Missing binary: {binary}"
    assert not (output / "bin/moonlight").exists(), "Upstream executable collision"
    entry = configparser.ConfigParser(interpolation=None)
    entry.read(output / "share/applications/io.github.keithxc.DeskPort.desktop")
    assert entry["Desktop Entry"]["Exec"] == "deskport"
    assert entry["Desktop Entry"]["StartupWMClass"] == "io.github.keithxc.DeskPort"
    assert (output / "share/icons/hicolor/scalable/apps/deskport.svg").is_file()

    # Keep the private Wayland socket short, including long managed TMPDIR paths.
    with tempfile.TemporaryDirectory(prefix="ds-") as temporary:
        root = Path(temporary)
        env = os.environ.copy()
        for key, name in (
            ("XDG_CONFIG_HOME", "config"),
            ("XDG_CACHE_HOME", "cache"),
            ("XDG_DATA_HOME", "data"),
            ("XDG_STATE_HOME", "state"),
            ("XDG_RUNTIME_DIR", "r"),
        ):
            path = root / name
            path.mkdir(mode=0o700)
            env[key] = str(path)
        env.update(QT_QPA_PLATFORM="offscreen", SDL_VIDEODRIVER="dummy")
        for key in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS"):
            env.pop(key, None)
        for flag in ("--version", "--help"):
            result = subprocess.run(
                [str(binary), flag], env=env, cwd=root,
                capture_output=True, text=True, timeout=30, check=True,
            )
            text = result.stdout + result.stderr
            if flag == "--version":
                assert expected_version in text.split(), text
            else:
                assert "Usage:" in text and "deskport" in text, text
        seamless = output / "libexec/deskport-seamless-host"
        assert seamless.is_file(), f"Missing Seamless sidecar: {seamless}"
        result = subprocess.run(
            [str(seamless), "--self-test", "--socket", "s",
             "--width", "320", "--height", "200"],
            env=env, cwd=root, capture_output=True, text=True, timeout=30,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        events = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
        event_types = [event.get("type") for event in events]
        for required in ("ready", "window-create", "frame-metadata", "window-destroy",
                         "child-exit", "self-test-pass", "stopping"):
            assert required in event_types, (required, events)
        passed = next(event for event in events if event.get("type") == "self-test-pass")
        assert passed.get("networkStreaming") is False and len(passed.get("sha256", "")) == 64
        assert not any(Path(env["XDG_RUNTIME_DIR"]).iterdir()), "Seamless socket was not removed"
        assert not (root / "config/Moonlight Game Streaming Project").exists()
    print("PASS: DeskPort executable, desktop identity and isolated CLI startup")


if __name__ == "__main__":
    main()
