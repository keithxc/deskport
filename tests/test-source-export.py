#!/usr/bin/env python3
"""Verify source export isolation and pin checks using disposable local repos."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import tarfile

ROOT = Path(__file__).resolve().parents[1]
base = Path(subprocess.check_output(["bash", str(ROOT / "scripts/build-paths.sh"), "checkout"],
                                    text=True).strip()) / "source-export-tests.noindex"
base.mkdir(parents=True, exist_ok=True)
checks = 0


def check(condition, message):
    global checks
    if not condition:
        raise AssertionError(message)
    checks += 1


with tempfile.TemporaryDirectory(prefix="export-", dir=base) as temporary:
    root = Path(temporary)
    repo, core, build = root / "repo", root / "core", root / "build"

    def git(where, *args):
        return subprocess.run(["git", "-C", str(where), *args], check=True,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    for checkout in (repo, core):
        checkout.mkdir()
        git(checkout, "init", "--quiet")
        git(checkout, "config", "user.name", "Source Export Fixture")
        git(checkout, "config", "user.email", "fixture@example.invalid")
    (core / "public.txt").write_text("pinned public core\n")
    git(core, "add", ".")
    git(core, "commit", "--quiet", "-m", "Create fixture core")
    (repo / "scripts").mkdir()
    (repo / "app").mkdir()
    (repo / "app/version.txt").write_text("1.2.3\n")
    for name in ("build-paths.sh", "generate-src.sh", "check-build-privacy.py"):
        shutil.copy2(ROOT / "scripts" / name, repo / "scripts" / name)
    shutil.copy2(ROOT / ".gitignore", repo / ".gitignore")
    git(repo, "-c", "protocol.file.allow=always", "submodule", "add", "--quiet", str(core), "shared/deskport-core")
    git(repo, "add", ".")
    git(repo, "commit", "--quiet", "-m", "Create fixture source")
    (repo / "build-signing.env").write_text("LOCAL_ONLY_SYNTHETIC_CONFIG\n")
    env = dict(os.environ, DESKPORT_BUILD_ROOT=str(build), DESKPORT_RUN_ID="source-fixture",
               DESKPORT_TARGET="linux-nix")
    for name in ("DESKPORT_WORK_DIR", "DESKPORT_DIST_DIR", "DESKPORT_LOG_DIR"):
        env.pop(name, None)
    target = build / "outputs/source.tar.gz"
    target.parent.mkdir(parents=True)
    sentinel = target.parent / "existing-package.txt"
    sentinel.write_text("retain earlier output\n")

    def export(path):
        return subprocess.run(["bash", str(repo / "scripts/generate-src.sh"), str(path)],
                              env=env, capture_output=True, text=True)

    result = export(target)
    check(result.returncode == 0 and target.is_file(), "clean source export must succeed")
    check(sentinel.read_text() == "retain earlier output\n", "source export must preserve neighboring outputs")
    with tarfile.open(target) as archive:
        names = archive.getnames()
        check("DeskPort-1.2.3/shared/deskport-core/public.txt" in names, "pinned shared core must be exported")
        check(not any("build-signing.env" in name or name.endswith("/.git") for name in names),
              "local signing config and Git metadata must not enter source material")
    checksum = hashlib.sha256(target.read_bytes()).digest()
    check(export(target).returncode != 0 and hashlib.sha256(target.read_bytes()).digest() == checksum,
          "existing source archive must not be overwritten")
    check(export(repo / "source.tar.gz").returncode != 0,
          "source output inside checkout must be refused")
    (repo / "app/version.txt").write_text("1.2.4\n")
    check(export(target.parent / "dirty.tar.gz").returncode != 0,
          "dirty tracked source must be refused")
    git(repo, "checkout", "--", "app/version.txt")
    (repo / "untracked.txt").write_text("new source\n")
    check(export(target.parent / "untracked.tar.gz").returncode != 0,
          "untracked source must be refused")
    (repo / "untracked.txt").unlink()
    pinned = repo / "shared/deskport-core"
    git(pinned, "config", "user.name", "Source Export Fixture")
    git(pinned, "config", "user.email", "fixture@example.invalid")
    (pinned / "public.txt").write_text("new unpinned core\n")
    git(pinned, "add", ".")
    git(pinned, "commit", "--quiet", "-m", "Change fixture core")
    check(export(target.parent / "unpinned.tar.gz").returncode != 0,
          "an unpinned submodule commit must be refused")
    check(not any(build.rglob(".source-stage.*")), "temporary staging must be removed after export")

print(f"PASS: {checks} source export checks; previous outputs and local config preserved")
