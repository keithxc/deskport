"""Signing configuration, logged build phases and packaging."""
from __future__ import annotations

import contextlib
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time

from . import common, runs
from .assets import asset_info, asset_suffix, collect_assets, suffix_assets, suffixed_name
from .common import OWNERSHIP, Refused, SCHEMA, SIGN_KEYS, digest, no_links, write_json
from .runs import freeze


def local_signing(env, filename=None):
    path = Path(os.path.abspath(Path(filename).expanduser())) if filename else Path.home() / ".config/deskport/build-signing.env"
    if path.exists():
        no_links(path)
        info = path.stat()
        if info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise Refused("Signing config must be owned by you with mode 600")
        build_root = Path(env.get("DESKPORT_BUILD_ROOT", Path.home() / "mygit/build/deskport")).absolute()
        if path == common.ROOT or common.ROOT in path.parents or path == build_root or build_root in path.parents:
            raise Refused("Keep signing config outside source and generated output directories")
        for number, line in enumerate(path.read_text().splitlines(), 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            key, separator, value = line.removeprefix("export ").partition("=")
            if not separator or key not in SIGN_KEYS:
                raise Refused(f"Unsupported signing config entry on line {number}")
            values = shlex.split(value, comments=True)
            if len(values) != 1 or "$" in values[0] or "`" in values[0]:
                raise Refused(f"Signing config must contain literal values on line {number}")
            env.setdefault(key, values[0])
    for key in SIGN_KEYS:
        if key in env and ("REPLACE" in env[key] or "YOUR_" in env[key] or env[key] in {"DEVELOPER_ID_CERTIFICATE_SHA1", "APPLE_TEAM_ID"}):
            raise Refused(f"Replace the placeholder for {key} in your private configuration")


def run_logged(command, paths, env, phase, *, stdout_path=None):
    paths["logs"].mkdir(parents=True, exist_ok=True)
    log = paths["logs"] / (phase + ".log")
    if log.exists():
        log = log.with_name(f"{phase}-{time.time_ns()}.log")
    print(f"{phase}: {log}", flush=True)
    # Do not log the environment or command arguments containing signing values.
    started = datetime.now(timezone.utc).isoformat()
    with contextlib.ExitStack() as stack:
        stream = stack.enter_context(log.open("wb"))
        output = stream
        if stdout_path is not None:
            no_links(stdout_path)
            stdout_path.parent.mkdir(parents=True, exist_ok=True)
            output = stack.enter_context(stdout_path.open("xb"))
        child = subprocess.Popen([str(x) for x in command], cwd=common.ROOT, env=env,
                                 stdout=output, stderr=stream, start_new_session=True)
        try:
            while True:
                try:
                    code = child.wait(timeout=5)
                    break
                except subprocess.TimeoutExpired:
                    reserve = float(env.get("DESKPORT_MIN_FREE_GIB", "0")) * 2**30
                    if shutil.disk_usage(paths["base"]).free < reserve:
                        raise Refused("Build crossed the configured free-space reserve; stopping this command")
        except BaseException:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=15)
            except subprocess.TimeoutExpired:
                with contextlib.suppress(ProcessLookupError):
                    os.killpg(child.pid, signal.SIGKILL)
                child.wait()
            raise
    record_path = paths["record"] / "steps.json"
    steps = json.loads(record_path.read_text()) if record_path.exists() else []
    step = {"phase": phase, "exit_code": code, "started_at": started,
            "finished_at": datetime.now(timezone.utc).isoformat(), "log_sha256": digest(log)}
    if stdout_path is not None:
        step.update(stdout_path=str(stdout_path), stdout_sha256=digest(stdout_path))
    steps.append(step)
    write_json(record_path, steps)
    if code:
        raise Refused(f"{phase} failed ({code}); inspect the private log above")


def nix_output(stdout_path):
    """Accept exactly one existing package directory from Nix's stdout only."""
    try:
        outputs = stdout_path.read_text(encoding="ascii").splitlines()
    except UnicodeError as exc:
        raise Refused("Nix stdout is not an ASCII store path") from exc
    if len(outputs) != 1 or not re.fullmatch(
            r"/nix/store/[0-9abcdfghijklmnpqrsvwxyz]{32}-[A-Za-z0-9+._?=-]+", outputs[0]):
        raise Refused("Nix stdout must contain exactly one absolute store output path")
    output = Path(outputs[0])
    if output.is_symlink() or not output.is_dir():
        raise Refused("Nix output must be an existing store directory")
    return output


def host_check(target):
    if target == "macos":
        if platform.system() != "Darwin" or platform.machine() != "arm64":
            raise Refused("macOS packaging requires an Apple Silicon Mac")
    elif platform.system() != "Linux" or platform.machine() != "x86_64":
        raise Refused("Run Linux/Windows x86_64 builds on the Linux build host")


def tool_check(target, cleanup=False):
    required = ["git", "bash", "ps"]
    required += ["podman"] if target == "linux-portable" else ["nix-shell" if target == "windows" else "nix"]
    missing = [name for name in required if not shutil.which(name)]
    if cleanup and not (shutil.which("lsof") or Path("/usr/sbin/lsof").is_file()):
        missing.append("lsof (run cleanup inside nix develop on Linux)")
    if missing:
        raise Refused("Missing required tools: " + ", ".join(missing))


def finish_package(args, paths, env, record):
    """Record exact artifacts only after all platform packaging steps succeeded."""
    state = record["source"]
    if common.git_state() != state:
        raise Refused("Source changed since this run started; start a fresh package run")
    version = (common.ROOT / "app/version.txt").read_text().strip()
    if args.target == "macos" and record["status"] == "notarized":
        evidence = paths["record"] / "notarization"
        evidence.mkdir(parents=True, exist_ok=True)
        for name in ("app-submit.json", "app-info.json", "dmg-submit.json", "dmg-info.json", "STATUS.txt"):
            source_file = paths["dist"] / "notarized" / name
            if source_file.is_file():
                shutil.copyfile(source_file, evidence / name)
    suffix = asset_suffix(record.get("asset_suffix") or None)
    metadata = paths["dist"] / suffixed_name(f"DeskPort-{version}-{args.target}-build.json", suffix)
    checksums = paths["dist"] / suffixed_name(f"SHA256SUMS-{args.target}.txt", suffix)
    for destination in (metadata, checksums):
        no_links(destination)
        if destination.exists() and asset_info(paths, destination) not in record.get("assets", []):
            raise Refused(f"Unrecorded or changed generated asset already exists: {destination.name}")
    assets = collect_assets(paths, args.target, record["status"])
    if args.target == "linux-portable":
        for p in sorted(paths["dist"].iterdir()):
            if p.name in {suffixed_name(name, value) for name in ("AppImage-runtime-sources.json", "build-packages.txt")
                          for value in ("", suffix)}:
                assets.append(asset_info(paths, p))
    assets = suffix_assets(paths, assets, suffix)
    # Public metadata contains no machine paths, hostnames, signing identities,
    # keychain profiles or raw logs.
    write_json(metadata, {"schema": SCHEMA, "version": version, "target": args.target,
                          "asset_suffix": suffix,
                          "source": state,
                          "source_url": f"https://github.com/keithxc/deskport/tree/{state['revision']}",
                          "assets": [{k: a[k] for k in ("name", "size", "sha256")} for a in assets]})
    assets.append(asset_info(paths, metadata))
    checksums.write_text("".join(f"{a['sha256']}  {a['name']}\n" for a in assets))
    assets.append(asset_info(paths, checksums))
    record.update(assets=assets, source_unchanged=True, failed=False)


def build(args, paths, env):
    suffix = asset_suffix(getattr(args, "asset_suffix", None))
    host_check(args.target)
    tool_check(args.target)
    if getattr(args, "flatpak", False) and not shutil.which("flatpak"):
        raise Refused("Flatpak packaging requires the flatpak command")
    runs.capacity(paths, args)
    common.privacy()
    env["DESKPORT_MIN_FREE_GIB"] = str(args.min_free_gib if args.min_free_gib is not None else (80 if platform.system() == "Darwin" else 20))
    if paths["target"].exists() or (paths["record"] / "run.json").exists():
        raise Refused("Run ID already exists; use a new ID (notarize resumes a signed candidate)")
    state = common.git_state()
    if args.distribution and (args.target != "macos" or state["dirty"]):
        raise Refused("Distribution packaging requires macOS and a clean source checkout")
    if args.target == "macos":
        local_signing(env, args.signing_config)
    try:
        jobs = int(env.get("DESKPORT_JOBS", "4"))
        if jobs <= 0:
            raise ValueError
    except ValueError as exc:
        raise Refused("DESKPORT_JOBS must be a positive integer") from exc
    env["DESKPORT_JOBS"] = str(min(jobs, 4))
    paths["work"].mkdir(parents=True)
    scratch = paths["work"] / "tmp"
    scratch.mkdir()
    env["TMPDIR"] = str(scratch)
    paths["cache"].mkdir(parents=True, exist_ok=True)
    (paths["cache"] / ".keep").touch()
    record = dict(owner=OWNERSHIP, schema=SCHEMA, checkout=str(common.ROOT), run=args.run,
                  target=args.target, source=state, status="building", assets=[], asset_suffix=suffix,
                  created_at=datetime.now(timezone.utc).isoformat())
    write_json(paths["record"] / "run.json", record)
    try:
        if args.target == "macos":
            command = ["nix", "develop", "-c", "bash", "scripts/package-macos.sh"]
        elif args.target == "linux-nix":
            command = ["nix", "build", "-L", "--cores", "4", "--max-jobs", "1", "--no-link", "--print-out-paths"]
        elif args.target == "linux-portable":
            env["DESKPORT_LINUX_FORMAT"] = args.format
            command = ["bash", "scripts/package-linux.sh"]
        else:
            mode = "build" if args.command == "build" else "package"
            command = ["nix-shell", "winbuild/scripts/shell.nix", "--run",
                       f"bash winbuild/scripts/bootstrap.sh --mode {mode}"]
        if args.target == "linux-nix":
            stdout_path = paths["record"] / "nix-outputs.txt"
            run_logged(command, paths, env, "build", stdout_path=stdout_path)
            output = nix_output(stdout_path)
            record["nix_output"] = str(output)
            write_json(paths["record"] / "run.json", record)
        else:
            run_logged(command, paths, env, "build")
        if args.target == "linux-nix":
            # Unix-domain socket limits include every byte of TMPDIR. Keep
            # socket fixtures short while retaining them under the build root.
            short_base = paths["base"].parent / "t"
            no_links(short_base)
            short_base.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix="dp-", dir=short_base) as temporary:
                smoke_env = {**env, "TMPDIR": temporary}
                run_logged([sys.executable, "scripts/deskport-smoke.py", output], paths, smoke_env, "smoke")
        record["status"] = "built"
        if args.command == "package" and args.target != "linux-nix":
            if args.target == "linux-portable" and args.format == "all":
                version = (common.ROOT / "app/version.txt").read_text().strip()
                run_logged(["tar", "-C", paths["work"], "-czf",
                            paths["dist"] / f"DeskPort-{version}-linux-x86_64.tar.gz",
                            "DeskPort.AppDir"], paths, env, "portable-archive")
                if args.flatpak:
                    output = paths["work"] / "flatpak"
                    run_logged(["bash", "scripts/package-flatpak.sh",
                                paths["work"] / "DeskPort.AppDir", output, version],
                               paths, env, "flatpak")
                    bundle = output / f"DeskPort-{version}-client-x86_64.flatpak"
                    bundle.replace(paths["dist"] / bundle.name)
            if args.distribution:
                candidate = paths["dist"] / "notarized"
                run_logged(["nix", "develop", "-c", "bash", "scripts/release-macos.sh", "prepare",
                            paths["dist"] / "DeskPort.app", candidate], paths, env, "sign")
                record["status"] = "signed"
                write_json(paths["record"] / "run.json", record)
                run_logged(["nix", "develop", "-c", "bash", "scripts/release-macos.sh", "notarize", candidate], paths, env, "notarize")
                record["status"] = "notarized"
            finish_package(args, paths, env, record)
        record["source_unchanged"] = common.git_state() == state
        if not record["source_unchanged"]:
            raise Refused("Source changed during the build; do not publish this run")
    except BaseException:
        record["failed"] = True
        raise
    finally:
        freeze(paths, record)
    print(json.dumps({"run": args.run, "status": record["status"],
                      "dist": str(paths["dist"]), "record": str(paths["record"] / "run.json")}, indent=2))
