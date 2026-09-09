#!/usr/bin/env python3
"""Check the built Linux identity without a real desktop or remote host."""

import configparser
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    output = Path(sys.argv[1] if len(sys.argv) > 1 else "result").resolve()
    binary = output / "bin/deskport"
    assert binary.is_file(), f"Missing binary: {binary}"
    assert not (output / "bin/moonlight").exists(), "Upstream executable collision"
    entry = configparser.ConfigParser(interpolation=None)
    entry.read(output / "share/applications/io.github.keithxc.DeskPort.desktop")
    assert entry["Desktop Entry"]["Exec"] == "deskport"
    assert entry["Desktop Entry"]["StartupWMClass"] == "io.github.keithxc.DeskPort"
    assert (output / "share/icons/hicolor/scalable/apps/deskport.svg").is_file()

    with tempfile.TemporaryDirectory(prefix="deskport-smoke-") as temporary:
        root = Path(temporary)
        env = os.environ.copy()
        for key, name in (
            ("XDG_CONFIG_HOME", "config"),
            ("XDG_CACHE_HOME", "cache"),
            ("XDG_DATA_HOME", "data"),
            ("XDG_STATE_HOME", "state"),
            ("XDG_RUNTIME_DIR", "runtime"),
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
                assert "0.1.0" in text, text
            else:
                assert "Usage:" in text and "deskport" in text, text
        assert not (root / "config/Moonlight Game Streaming Project").exists()
    print("PASS: DeskPort executable, desktop identity and isolated CLI startup")


if __name__ == "__main__":
    main()
