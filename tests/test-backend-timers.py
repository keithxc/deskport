#!/usr/bin/env python3
"""Build and run synthetic backend timer tests in an explicitly owned output directory."""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import build_storage

import argparse
import os
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="Exclusive external timer-test directory")
    args = parser.parse_args()
    # Validate without bootstrap's shared cache/tmp creation: this task owns
    # only the caller-selected timer directory, including compiler caches.
    storage = build_storage.Storage()
    args.output = storage.validate(args.output)
    environment = dict(os.environ, **storage.environment())
    args.output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="b7-", dir=args.output))
    for name in ("tmp", "cache", "config", "data"):
        (work / name).mkdir()
    environment.update(QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
                       QML_DISABLE_DISK_CACHE="1", XDG_CONFIG_HOME=str(work / "config"),
                       XDG_CACHE_HOME=str(work / "cache"), XDG_DATA_HOME=str(work / "data"),
                       TMPDIR=str(work / "tmp"), TMP=str(work / "tmp"), TEMP=str(work / "tmp"),
                       DESKPORT_INPUT_TEST_PROFILE=str(work))
    environment.pop("DESKPORT_SESSION_ENDPOINT", None)
    for key in ("CLANG_MODULE_CACHE_PATH", "SWIFT_MODULECACHE_PATH", "SWIFT_MODULE_CACHE_PATH",
                "COMPILATION_CACHE_CAS_PATH", "CCACHE_DIR", "SCCACHE_DIR", "PYTHONPYCACHEPREFIX"):
        environment[key] = str(work / "cache" / key.lower())
    (work / ".deskport-input-test").write_text("Synthetic backend timer tests\n")
    environment.setdefault("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer")
    prefixes = environment.get("QT_ADDITIONAL_PACKAGES_PREFIX_PATH", "").split(os.pathsep)
    plugins = [str(Path(prefix) / "lib/qt-6/plugins") for prefix in prefixes
               if prefix and (Path(prefix) / "lib/qt-6/plugins").is_dir()]
    if plugins:
        environment["QT_PLUGIN_PATH"] = os.pathsep.join(plugins + ([environment["QT_PLUGIN_PATH"]]
                                                                  if environment.get("QT_PLUGIN_PATH") else []))
    with (work / "build.log").open("w") as log:
        subprocess.run([environment.get("DESKPORT_QMAKE", "qmake"),
                        str(Path(__file__).with_name("backend-timers.pro"))],
                       cwd=work, env=environment, check=True, stdout=log, stderr=log)
        subprocess.run(["make", "-j2"], cwd=work, env=environment, check=True, stdout=log, stderr=log)
    result = subprocess.run([str(work / "backend-timers")], cwd=work, env=environment,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
    (work / "test.log").write_text(result.stdout)
    print(result.stdout)
    print(f"Backend timer test evidence: {work}")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
