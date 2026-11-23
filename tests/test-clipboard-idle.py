#!/usr/bin/env python3
"""Isolated TLS/pipe and in-memory clipboard tests; no personal sessions."""
import sys
sys.dont_write_bytecode = True

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, required=True,
                        help="Exclusive clipboard test directory under the resolved external project root")
    parser.add_argument("--large-frames", action="store_true")
    parser.add_argument("--long-timeout", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(root / "scripts"))
    import build_storage
    storage = build_storage.bootstrap()
    env = dict(os.environ)
    qmake = env.get("DESKPORT_QMAKE") or shutil.which("qmake6") or "qmake"
    base = storage.validate(args.work_dir)
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="clipboard-idle-", dir=base) as temporary:
        work = Path(temporary)
        for name in ("tmp", "cache", "config", "runtime"):
            (work / name).mkdir(mode=0o700)
        env.update(TMPDIR=str(work / "tmp"), TMP=str(work / "tmp"), TEMP=str(work / "tmp"),
                   XDG_CACHE_HOME=str(work / "cache"), XDG_CONFIG_HOME=str(work / "config"),
                   XDG_RUNTIME_DIR=str(work / "runtime"), QT_QPA_PLATFORM="offscreen",
                   CLANG_MODULE_CACHE_PATH=str(work / "cache/modules"))
        # Ignore optional flags inherited from another invocation.
        env.pop("DESKPORT_CLIPBOARD_IDLE_LARGE", None)
        env.pop("DESKPORT_CLIPBOARD_IDLE_LONG_TIMEOUT", None)
        if args.large_frames:
            env["DESKPORT_CLIPBOARD_IDLE_LARGE"] = "1"
        if args.long_timeout:
            env["DESKPORT_CLIPBOARD_IDLE_LONG_TIMEOUT"] = "1"
        for name in ("peer", "other"):
            subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                            "-keyout", str(work / f"{name}.key"), "-out", str(work / f"{name}.crt"),
                            "-days", "1", "-subj", "/CN=clipboard-idle.invalid"],
                           cwd=work, env=env, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        env.update(IDLE_TEST_CERT=str(work / "peer.crt"), IDLE_TEST_KEY=str(work / "peer.key"),
                   IDLE_TEST_BAD_CERT=str(work / "other.crt"))
        project = work / "clipboard-idle.pro"
        sources = ("tests/clipboard-idle.cpp", "app/backend/clipboardchannel.cpp", "app/clipboard/agent.cpp")
        project.write_text("QT += core gui network testlib\nCONFIG += console c++17 testcase\n"
                           "CONFIG -= app_bundle\nTARGET = clipboard-idle\n" +
                           f'INCLUDEPATH += "{root}/app"\n' +
                           'HEADERS += "' + str(root / "app/backend/clipboardchannel.h") + '"\n' +
                           "SOURCES += " + " ".join(f'"{root / source}"' for source in sources) + "\n")
        subprocess.run([qmake, str(project)], cwd=work, env=env, check=True)
        subprocess.run(["make", "-j2"], cwd=work, env=env, check=True)
        subprocess.run([str(work / "clipboard-idle")], cwd=work, env=env, check=True,
                       timeout=240 if args.long_timeout else 100)


if __name__ == "__main__":
    main()
