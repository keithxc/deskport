"""Run directory layout, locks, records and capacity budgets."""
from __future__ import annotations

import contextlib
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil

from . import common
import build_storage
from .common import NAME, OWNERSHIP, Refused, SCHEMA, check, no_links, write_json


def layout(target, run, dependency_cache=None):
    if not NAME.fullmatch(run) or run in {".", ".."}:
        raise Refused("Run ID must be a single alphanumeric/dot/dash/underscore name")
    env = {**os.environ, "DESKPORT_TARGET": target, "DESKPORT_RUN_ID": run}
    # Ignore inherited individual output overrides: a managed run must own every
    # output. Standalone platform scripts continue supporting those overrides.
    for key in ("DESKPORT_MACOS_BUILD_DIR", "DESKPORT_MACOS_DIST_DIR", "DESKPORT_HOST_SOURCE_DIR",
                "DESKPORT_HOST_BUILD_DIR", "DESKPORT_SOURCE", "DESKPORT_WORK", "DESKPORT_WINDOWS_SOURCE_ROOT",
                "DESKPORT_WINDOWS_BUILD_ROOT", "DESKPORT_WINDOWS_AUDIT_DIR", "DESKPORT_WINDOWS_INPUT_CACHE",
                "DESKPORT_WINDOWS_DEPENDENCY_BUILD", "DESKPORT_WINDOWS_CACHE_LOCK_HELD",
                "DESKPORT_WINDOWS_DEPENDENCY_ROOT", "DESKPORT_WINDOWS_DEPENDENCY_CACHE",
                "DESKPORT_LINUX_WORK", "DESKPORT_DEPENDENCY_CACHE_DIR", "PACKAGE_SUFFIX", "MATERIAL_SUFFIX"):
        env.pop(key, None)
    base = Path(check(["bash", common.ROOT / "scripts/build-paths.sh", "checkout"], env=env))
    no_links(base)
    if base == common.ROOT or common.ROOT in base.parents or str(base).startswith("/nix/store/"):
        raise Refused("Build outputs must be outside source and the Nix store")
    target_root = base / "runs.noindex" / run / target
    # Large library caches live independently of disposable application runs.
    cache = base / "dependencies.noindex" / target
    if dependency_cache is not None:
        cache = Path(dependency_cache).expanduser().absolute()
        try:
            build_storage.Storage().validate(cache)
        except (OSError, ValueError) as error:
            raise Refused(str(error)) from error
        no_links(cache)
        # Borrow an existing retained cache in place. Never adopt a run or move
        # CMake/qmake trees whose configured paths belong to another checkout.
        if (cache.name != target or cache.parent.name != "dependencies.noindex" or
                "runs.noindex" in cache.parts or not (cache / ".keep").is_file() or
                cache == common.ROOT or common.ROOT in cache.parents or str(cache).startswith("/nix/store/")):
            raise Refused("Dependency cache must be an existing retained dependencies.noindex/<target> directory")
    if target == "linux-portable":
        key = hashlib.sha256(b"deskport-linux-portable-dependencies-v1\0")
        recipes = [common.ROOT / "flake.lock", common.ROOT / "scripts/linux-tools.json", common.ROOT / "scripts/package-linux-container.sh"]
        recipes += list((common.ROOT / "scripts").glob("build-linux-*.sh"))
        recipes += list((common.ROOT / "scripts").glob("build-appimage-*.sh"))
        recipes += list((common.ROOT / "host/linux").glob("*.patch"))
        for recipe in sorted(set(recipes)):
            key.update(str(recipe.relative_to(common.ROOT)).encode() + b"\0" + recipe.read_bytes())
        cache /= key.hexdigest()[:24]
    paths = {k: target_root / k for k in ("work", "dist", "logs", "tests")}
    paths.update(base=base, target=target_root, cache=cache,
                 record=base / "records" / run / target)
    for value in paths.values():
        no_links(value)
    env.update(DESKPORT_DEPENDENCY_CACHE_DIR=str(cache), DESKPORT_NIX_DEPS="1",
               DESKPORT_WORK_DIR=str(paths["work"]), DESKPORT_DIST_DIR=str(paths["dist"]),
               DESKPORT_LOG_DIR=str(paths["logs"]), DESKPORT_TEST_DIR=str(paths["tests"]),
               DESKPORT_MACOS_BUILD_DIR=str(paths["work"]), DESKPORT_MACOS_DIST_DIR=str(paths["dist"]),
               DESKPORT_LINUX_WORK=str(paths["work"]), DESKPORT_WINDOWS_BUILD_ROOT=str(paths["work"]),
               DESKPORT_WINDOWS_SOURCE_ROOT=str(common.ROOT),
               PACKAGE_OUT=str(paths["dist"]), PACKAGE_STAGE=str(paths["work"] / "stage"),
               PYTHONDONTWRITEBYTECODE="1")
    env["DESKPORT_BUILD_ROOT"] = str(base.parent)
    env.update(build_storage.Storage(task=run, target=target).environment(env))
    return paths, env


@contextlib.contextmanager
def locked(paths, target):
    directory = paths["base"] / "locks"
    no_links(directory)
    directory.mkdir(parents=True, exist_ok=True)
    lock = directory / (target + ".lock")
    no_links(lock)
    with lock.open("a+") as stream:
        try:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise Refused(f"Another managed {target} operation owns the lock") from exc
        yield


def read_record(paths, args):
    path = paths["record"] / "run.json"
    try:
        record = json.loads(path.read_text())
    except FileNotFoundError as exc:
        raise Refused("No managed run record; legacy directories require a separate reviewed cleanup") from exc
    if (record.get("owner") != OWNERSHIP or record.get("schema") != SCHEMA or
            record.get("checkout") != str(common.ROOT) or record.get("run") != args.run or
            record.get("target") != args.target):
        raise Refused("Run ownership does not match this checkout/target")
    return record


def freeze(paths, record):
    record["trees"] = {key: common.footprint(paths[key]) for key in ("work", "dist", "logs", "tests")}
    record["finished_at"] = datetime.now(timezone.utc).isoformat()
    write_json(paths["record"] / "run.json", record)


def capacity(paths, args):
    parent = paths["base"]
    while not parent.exists():
        parent = parent.parent
    reserve = args.min_free_gib if args.min_free_gib is not None else (80 if platform.system() == "Darwin" else 20)
    free = shutil.disk_usage(parent).free
    if free < reserve * 2**30:
        raise Refused(f"Free space {free / 2**30:.1f} GiB is below the {reserve:g} GiB build reserve")
    total = common.footprint(paths["base"] / "dependencies.noindex")["bytes"]
    if total > args.max_cache_gib * 2**30:
        raise Refused(f"Dependency caches exceed {args.max_cache_gib:g} GiB; inspect cache usage before building")
    pending = common.footprint(paths["base"] / "runs.noindex")["bytes"]
    if pending > args.max_work_gib * 2**30:
        raise Refused(f"Managed run outputs exceed {args.max_work_gib:g} GiB; clean completed runs before building")
