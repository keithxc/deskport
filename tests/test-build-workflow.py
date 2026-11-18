#!/usr/bin/env python3
"""Adversarial tests for publication receipts and owned-run cleanup."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import argparse
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import types
import unittest
from unittest.mock import patch
from contextlib import redirect_stderr, redirect_stdout

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from deskport_build import assets, cleanup, cli, common, package, release, runs  # noqa: E402

# Call through one namespace; replace a function on the module that defines it.
w = types.SimpleNamespace(**{name: value for module in (common, runs, assets, package, release, cleanup, cli)
                             for name, value in vars(module).items() if not name.startswith("__")})
TEST_ROOT = Path(subprocess.check_output(["bash", ROOT / "scripts/build-paths.sh", "checkout"]).decode().strip()) / "tests"
TEST_ROOT.mkdir(parents=True, exist_ok=True)


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="workflow-", dir=TEST_ROOT)
        self.base = Path(self.temp.name)
        target = self.base / "runs.noindex/run/macos"
        self.paths = {key: target / key for key in ("work", "dist", "logs", "tests")}
        self.paths.update(base=self.base, target=target, cache=self.base / "cache", record=self.base / "records/run/macos")
        for key in ("work", "dist", "cache", "record"):
            self.paths[key].mkdir(parents=True)
        (self.paths["work"] / "object.o").write_bytes(b"object")
        (self.paths["cache"] / "library.a").write_bytes(b"retained library")
        self.package = self.paths["dist"] / "DeskPort-test.zip"
        self.package.write_bytes(b"package bytes")
        self.source = dict(revision="a" * 40, dirty=False, diff_sha256="b" * 64, core="c" * 40)
        self.record = dict(owner=w.OWNERSHIP, schema=w.SCHEMA, checkout=str(ROOT), run="run", target="macos",
                           source=self.source, source_unchanged=True, status="notarized", assets=[w.asset_info(self.paths, self.package)])
        w.freeze(self.paths, self.record)
        self.args = argparse.Namespace(run="run", target="macos", apply=True, work_only=False, repo="owner/repo", tag="v1")
        self.asset = dict(id=12, name=self.package.name, size=self.package.stat().st_size,
                          digest="sha256:" + w.digest(self.package), state="uploaded",
                          browser_download_url="https://github.com/owner/repo/releases/download/v1/DeskPort-test.zip")
        self.release = dict(id=1, draft=False, assets=[self.asset])

    def tearDown(self):
        self.temp.cleanup()

    def receipt(self):
        w.write_json(self.paths["record"] / "github-receipt.json", {"repo": "owner/repo", "tag": "v1"})

    def remote_patches(self):
        return patch.object(release, "release_metadata", return_value=self.release), patch.object(
            release, "remote_hash", return_value=(w.digest(self.package), self.package.stat().st_size))

    def test_path_query_creates_nothing_and_ignores_output_override(self):
        root = self.base / "fresh"
        with patch.dict(os.environ, {"DESKPORT_BUILD_ROOT": str(root), "DESKPORT_MACOS_DIST_DIR": str(ROOT)}):
            paths, env = w.layout("macos", "valid-id")
            self.assertFalse(root.exists())
            self.assertEqual(env["DESKPORT_MACOS_DIST_DIR"], str(paths["dist"]))
            self.assertIn("runs.noindex", str(paths["dist"]))

    def test_path_traversal_and_source_outputs_refused(self):
        for run in ("../escape", ".", "..", "", "/absolute"):
            with self.assertRaises(w.Refused):
                w.layout("macos", run)
        with patch.dict(os.environ, {"DESKPORT_BUILD_ROOT": str(ROOT / "build")}):
            with self.assertRaises((w.Refused, subprocess.CalledProcessError)):
                w.layout("macos", "run")

    def test_reuse_retained_dependency_cache_without_relocation(self):
        cache = self.base / "other/dependencies.noindex/windows"
        cache.mkdir(parents=True)
        (cache / ".keep").touch()
        with patch.dict(os.environ, {"DESKPORT_BUILD_ROOT": str(self.base / "new")}):
            paths, env = w.layout("windows", "reuse", str(cache))
        self.assertEqual(paths["cache"], cache)
        self.assertEqual(env["DESKPORT_DEPENDENCY_CACHE_DIR"], str(cache))
        self.assertNotEqual(paths["work"], cache)
        self.assertFalse(paths["work"].exists())
        for invalid in (self.paths["work"], cache.parent, cache / "missing", ROOT):
            with self.assertRaises(w.Refused):
                w.layout("windows", "reuse", str(invalid))
        link = self.base / "linked"
        link.symlink_to(cache.parent)
        with self.assertRaises(w.Refused):
            w.layout("windows", "reuse", str(link / "windows"))

    def test_private_config_is_data_not_shell(self):
        config = self.base / "signing.env"
        config.write_text("DESKPORT_SIGN_" + "IDENTITY='$(touch BAD)'\n")
        config.chmod(0o600)
        with self.assertRaises(w.Refused):
            w.local_signing({"DESKPORT_BUILD_ROOT": str(self.base / "outputs")}, str(config))
        self.assertFalse((ROOT / "BAD").exists())
        config.write_text("DESKPORT_SIGN_" + "IDENTITY='literal identity'\n")
        env = {"DESKPORT_BUILD_ROOT": str(self.base / "outputs")}
        w.local_signing(env, str(config))
        self.assertEqual(env["DESKPORT_SIGN_IDENTITY"], "literal identity")
        config.chmod(0o644)
        with self.assertRaises(w.Refused):
            w.local_signing(env, str(config))

    def test_signing_config_cannot_hide_in_source_via_relative_path(self):
        relative = os.path.relpath(self.base / "signing.env", ROOT)
        (self.base / "signing.env").write_text("# private configuration\n")
        (self.base / "signing.env").chmod(0o600)
        with patch.object(common, "ROOT", self.base), self.assertRaises(w.Refused):
            w.local_signing({"DESKPORT_BUILD_ROOT": str(self.base / "outputs")}, relative)

    def test_cleanup_mount_root_is_refused(self):
        original = os.path.ismount
        with patch.object(os.path, "ismount", side_effect=lambda p: Path(p) == self.paths["work"] or original(p)):
            with self.assertRaises(w.Refused):
                w.deletion_guard(self.paths["work"], self.paths)

    def test_missing_upload_receipt_keeps_everything(self):
        with self.assertRaises(w.Refused):
            w.clean(self.args, self.paths, self.record)
        self.assertTrue(self.package.exists())
        self.assertTrue(self.paths["work"].exists())

    def test_remote_replacement_or_missing_asset_refused(self):
        self.receipt()
        self.asset["digest"] = "sha256:" + "0" * 64
        with patch.object(release, "release_metadata", return_value=self.release), patch.object(release, "remote_hash", return_value=("bad", 13)):
            with self.assertRaises(w.Refused):
                w.clean(self.args, self.paths, self.record)
        self.release["assets"] = []
        with patch.object(release, "release_metadata", return_value=self.release):
            with self.assertRaises(w.Refused):
                w.verify_remote(self.args, self.paths, self.record)
        self.assertTrue(self.package.exists())

    def test_draft_not_completed_delivery(self):
        self.release["draft"] = True
        with patch.object(release, "release_metadata", return_value=self.release), patch.object(release, "remote_hash") as download:
            with self.assertRaises(w.Refused):
                w.verify_remote(self.args, self.paths, self.record)
            download.assert_not_called()

    def test_changed_work_refused(self):
        self.args.work_only = True
        (self.paths["work"] / "user-notes.txt").write_text("retain")
        with self.assertRaises(w.Refused):
            w.clean(self.args, self.paths, self.record)
        self.assertTrue(self.paths["work"].exists())

    def test_protected_files_refused_even_after_freeze(self):
        for name in (".keep", ".git", "personal.p12"):
            item = self.paths["work"] / name
            item.write_text("preserve")
            w.freeze(self.paths, self.record)
            self.args.work_only = True
            with self.assertRaises(w.Refused):
                w.clean(self.args, self.paths, self.record)
            item.unlink()

    def test_allocation_only_change_after_freeze_allows_cleanup(self):
        self.args.work_only = True
        self.record["trees"]["work"]["bytes"] -= 4096
        with patch.object(cleanup, "inactive"), patch.object(common, "git_state", return_value=self.source), redirect_stdout(io.StringIO()):
            w.clean(self.args, self.paths, self.record)
        self.assertFalse(self.paths["work"].exists())
        self.assertTrue(self.package.exists())
        self.assertTrue((self.paths["cache"] / "library.a").exists())

    def test_allocation_only_change_after_plan_allows_cleanup(self):
        self.args.work_only = True
        original = w.footprint
        calls = 0

        def allocation_settles(path):
            nonlocal calls
            current = original(path)
            if path == self.paths["work"]:
                calls += 1
                if calls > 1:
                    current["bytes"] += 4096
            return current

        with patch.object(common, "footprint", side_effect=allocation_settles), patch.object(cleanup, "inactive"), patch.object(
                common, "git_state", return_value=self.source), redirect_stdout(io.StringIO()):
            w.clean(self.args, self.paths, self.record)
        self.assertGreater(calls, 1)
        self.assertFalse(self.paths["work"].exists())
        self.assertTrue(self.package.exists())

    def test_metadata_change_after_plan_still_refuses_cleanup(self):
        self.args.work_only = True
        calls = 0

        def change_work_during_recheck(path):
            nonlocal calls
            if path == self.paths["work"]:
                calls += 1
                if calls > 1:
                    (path / "user-notes.txt").write_text("retain")

        with patch.object(cleanup, "inactive", side_effect=change_work_during_recheck), patch.object(
                common, "git_state", return_value=self.source), redirect_stdout(io.StringIO()), self.assertRaises(w.Refused):
            w.clean(self.args, self.paths, self.record)
        self.assertEqual((self.paths["work"] / "user-notes.txt").read_text(), "retain")
        self.assertTrue(self.package.exists())

    def test_inbound_reference_from_other_run_refused(self):
        (self.paths["cache"] / "borrowed").symlink_to(self.paths["work"])
        with self.assertRaises(w.Refused):
            w.deletion_guard(self.paths["work"], self.paths)

    def test_same_run_link_is_safe_only_when_both_are_planned(self):
        (self.paths["work"] / "output").symlink_to(self.paths["dist"])
        with self.assertRaises(w.Refused):
            w.deletion_guard(self.paths["dist"], self.paths)
        w.deletion_guard(self.paths["dist"], self.paths, [self.paths["work"], self.paths["dist"]])

    def test_work_only_deletes_owned_objects_and_preserves_cache_packages_source(self):
        (self.paths["work"] / "cache").symlink_to(self.paths["cache"])
        (self.paths["work"] / "scripts").symlink_to(ROOT / "scripts")
        w.freeze(self.paths, self.record)
        self.args.work_only = True
        with patch.object(cleanup, "inactive"), patch.object(common, "git_state", return_value=self.source), redirect_stdout(io.StringIO()):
            w.clean(self.args, self.paths, self.record)
        self.assertFalse(self.paths["work"].exists())
        self.assertTrue(self.package.exists())
        self.assertEqual((self.paths["cache"] / "library.a").read_bytes(), b"retained library")
        self.assertTrue((ROOT / "scripts/build.sh").exists())

    def test_full_cleanup_rechecks_remote_and_leaves_only_cache_and_records(self):
        self.receipt()
        (self.paths["work"] / "output").symlink_to(self.paths["dist"])
        w.freeze(self.paths, self.record)
        a, b = self.remote_patches()
        with a, b as downloaded, patch.object(cleanup, "inactive"), patch.object(common, "git_state", return_value=self.source), redirect_stdout(io.StringIO()):
            w.clean(self.args, self.paths, self.record)
            downloaded.assert_not_called()
        self.assertFalse(self.paths["target"].exists())
        self.assertTrue((self.paths["cache"] / "library.a").exists())
        self.assertTrue((self.paths["record"] / "github-receipt.json").exists())
        self.assertTrue((self.paths["record"] / "cleanup-journal.json").exists())

    def test_plan_does_not_delete(self):
        self.args.work_only, self.args.apply = True, False
        with patch.object(cleanup, "inactive"), redirect_stdout(io.StringIO()):
            w.clean(self.args, self.paths, self.record)
        self.assertTrue(self.paths["work"].exists())

    def test_mixed_linux_package_names_are_collected(self):
        for name in ("deskport_1_amd64.deb", "deskport-1.x86_64.rpm", "deskport-1.pkg.tar.zst", "notary-private.log"):
            (self.paths["dist"] / name).write_bytes(b"package")
        names = {a["name"] for a in w.collect_assets(self.paths, "linux-portable", "built")}
        self.assertTrue({"deskport_1_amd64.deb", "deskport-1.x86_64.rpm", "deskport-1.pkg.tar.zst"} <= names)
        self.assertNotIn("notary-private.log", names)

    def test_source_materials_are_excluded_from_packages_and_uploads(self):
        for name in ("DeskPort-1-source.tar.gz", "DeskPort-1-linux-source.tar.gz", "source-materials-1-windows.tar.gz", "relink-materials-1-windows.tar.gz"):
            path = self.paths["dist"] / name
            path.write_bytes(b"not a release asset")
            self.assertTrue(w.source_material_archive(name))
        names = {a["name"] for a in w.collect_assets(self.paths, "windows", "built")}
        self.assertEqual(names, {self.package.name})
        self.record["assets"].append(w.asset_info(self.paths, self.paths["dist"] / "DeskPort-1-source.tar.gz"))
        with patch.object(common, "privacy"), patch.object(w.subprocess, "run") as upload:
            with self.assertRaises(w.Refused):
                w.publish(self.args, self.paths, self.record)
            upload.assert_not_called()

    def test_finishing_package_never_generates_source_or_relink_archives(self):
        self.args.target = "windows"
        self.record["status"] = "built"
        with patch.object(common, "git_state", return_value=self.source), patch.object(package, "run_logged") as command:
            w.finish_package(self.args, self.paths, {}, self.record)
            command.assert_not_called()
        self.assertEqual(len(self.record["assets"]), 3)
        self.assertFalse(any(w.source_material_archive(a["name"]) for a in self.record["assets"]))
        metadata = next(a for a in self.record["assets"] if a["name"].endswith("-build.json"))
        contents = json.loads((self.paths["dist"] / metadata["path"]).read_text())
        self.assertTrue(contents["source_url"].endswith(self.source["revision"]))

    def test_server_digest_verifies_without_downloading(self):
        with patch.object(release, "release_metadata", return_value=self.release), patch.object(release, "remote_hash") as download:
            receipt = w.verify_remote(self.args, self.paths, self.record)
            self.assertEqual(receipt["assets"][0]["verification"], "github-sha256")
            download.assert_not_called()

    def test_suffix_preserves_extensions_contents_version_and_repeat_finish(self):
        self.package.unlink()
        self.args.target = "linux-portable"
        self.record.update(status="built", asset_suffix="r2", assets=[])
        originals = ["DeskPort-0.7.4-x86_64.AppImage", "deskport-0.7.4.pkg.tar.zst",
                     "DeskPort-0.7.4-linux-x86_64.tar.gz", "deskport_0.7.4_amd64.deb",
                     "AppImage-runtime-sources.json", "build-packages.txt"]
        for name in originals:
            (self.paths["dist"] / name).write_bytes(name.encode())
        with patch.object(common, "git_state", return_value=self.source):
            w.finish_package(self.args, self.paths, {}, self.record)
            first = list(self.record["assets"])
            w.finish_package(self.args, self.paths, {}, self.record)
        self.assertEqual(self.record["assets"], first)
        names = {a["name"] for a in first}
        self.assertIn("deskport-0.7.4-r2.pkg.tar.zst", names)
        self.assertIn("DeskPort-0.7.4-linux-x86_64-r2.tar.gz", names)
        self.assertIn("AppImage-runtime-sources-r2.json", names)
        for name in originals:
            self.assertEqual((self.paths["dist"] / w.suffixed_name(name, "r2")).read_bytes(), name.encode())
            self.assertFalse((self.paths["dist"] / name).exists())
        version = (ROOT / "app/version.txt").read_text().strip()
        metadata = json.loads((self.paths["dist"] / f"DeskPort-{version}-linux-portable-build-r2.json").read_text())
        self.assertEqual(metadata["version"], version)
        self.assertEqual(metadata["asset_suffix"], "r2")
        checksums = (self.paths["dist"] / "SHA256SUMS-linux-portable-r2.txt").read_text().splitlines()
        self.assertEqual({line.split("  ")[1] for line in checksums}, names - {"SHA256SUMS-linux-portable-r2.txt"})
        for line in checksums:
            sha, name = line.split("  ")
            self.assertEqual(sha, w.digest(self.paths["dist"] / name))

    def test_suffix_conflicts_refuse_before_any_rename(self):
        extra = self.paths["dist"] / "DeskPort-aaa.zip"
        extra.write_bytes(b"first asset must remain")
        destination = self.package.with_name("DeskPort-test-r2.zip")
        destination.write_bytes(b"existing destination")
        self.args.target = "windows"
        self.record.update(status="built", asset_suffix="r2")
        with patch.object(common, "git_state", return_value=self.source), self.assertRaises(w.Refused):
            w.finish_package(self.args, self.paths, {}, self.record)
        self.assertTrue(extra.exists())
        self.assertTrue(self.package.exists())
        self.assertEqual(destination.read_bytes(), b"existing destination")
        destination.unlink()
        generated = self.paths["dist"] / "SHA256SUMS-windows-r2.txt"
        generated.write_text("unrelated record")
        with patch.object(common, "git_state", return_value=self.source), self.assertRaises(w.Refused):
            w.finish_package(self.args, self.paths, {}, self.record)
        self.assertTrue(extra.exists())
        self.assertTrue(self.package.exists())
        self.assertEqual(generated.read_text(), "unrelated record")

    def test_invalid_suffix_is_rejected_before_layout_or_run_creation(self):
        for value in ("", "../r2", "r.2", "-r2", "r 2", "r2/next", "a" * 33):
            with patch.object(runs, "layout") as layout, self.assertRaises(w.Refused):
                w.main(["package", "--target", "windows", "--asset-suffix=" + value])
            layout.assert_not_called()
        paths, args = self.fresh_build()
        args.asset_suffix = "../bad"
        with self.assertRaises(w.Refused):
            w.build(args, paths, {})
        self.assertFalse(paths["target"].exists())

    def test_notarize_inherits_suffix_and_does_not_recreate_completed_packages(self):
        candidate = self.paths["dist"] / "notarized"
        candidate.mkdir()
        self.package.unlink()
        self.record.update(status="signed", asset_suffix="r2", assets=[])
        w.freeze(self.paths, self.record)

        def notarize(*_args):
            (candidate / "DeskPort-test.zip").write_bytes(b"signed and notarized zip")
            (candidate / "DeskPort-test.dmg").write_bytes(b"signed and notarized dmg")

        command = ["notarize", "--target", "macos", "--run", "run"]
        with patch.object(runs, "layout", return_value=(self.paths, {})), patch.object(package, "host_check"), \
                patch.object(package, "local_signing"), patch.object(common, "git_state", return_value=self.source), \
                patch.object(package, "run_logged", side_effect=notarize) as native:
            w.main(command)
            completed = w.read_record(self.paths, self.args)
            self.assertEqual(completed["asset_suffix"], "r2")
            self.assertIn("notarized/DeskPort-test-r2.zip", {a["path"] for a in completed["assets"]})
            w.main(command + ["--asset-suffix", "r2"])
            self.assertEqual(native.call_count, 1)
            self.assertEqual(w.read_record(self.paths, self.args)["assets"], completed["assets"])
            with self.assertRaises(w.Refused):
                w.main(command + ["--asset-suffix", "r3"])
            self.assertEqual(native.call_count, 1)
            (candidate / "DeskPort-test-r2.zip").write_bytes(b"changed after notarization")
            with self.assertRaises(w.Refused):
                w.main(command)
            self.assertEqual(native.call_count, 1)

    def test_missing_or_invalid_digest_keeps_files_without_implicit_download(self):
        for value in (None, "", "sha256:bad", "md5:" + "a" * 32):
            self.asset["digest"] = value
            with patch.object(release, "release_metadata", return_value=self.release), patch.object(release, "remote_hash") as download:
                with self.assertRaises(w.Refused):
                    w.verify_remote(self.args, self.paths, self.record)
                download.assert_not_called()
            self.assertTrue(self.package.exists())

    def test_explicit_download_verification_for_legacy_asset(self):
        self.asset["digest"] = None
        self.args.download_verify = True
        a, b = self.remote_patches()
        with a, b as download:
            receipt = w.verify_remote(self.args, self.paths, self.record)
            self.assertEqual(receipt["assets"][0]["verification"], "download-sha256")
            download.assert_called_once()

    def test_refuses_symlink_asset_and_run(self):
        link = self.paths["dist"] / "DeskPort-secret.zip"
        link.symlink_to(self.base / "outside")
        with self.assertRaises(w.Refused):
            w.asset_info(self.paths, link)
        self.record["checkout"] = "/different/source"
        w.write_json(self.paths["record"] / "run.json", self.record)
        with self.assertRaises(w.Refused):
            w.read_record(self.paths, self.args)

    def test_lock_excludes_concurrent_run(self):
        with w.locked(self.paths, "macos"):
            with self.assertRaises(w.Refused):
                with w.locked(self.paths, "macos"):
                    pass

    def test_publish_validates_all_inputs_before_first_upload(self):
        self.record["assets"].append(dict(path="missing-package.zip", name="DeskPort-test-extra.zip", size=1, sha256="bad"))
        with patch.object(common, "privacy"), patch.object(w.subprocess, "run") as upload:
            with self.assertRaises(w.Refused):
                w.publish(self.args, self.paths, self.record)
            upload.assert_not_called()

    def test_cache_budget_blocks_build_without_touching_run(self):
        args = argparse.Namespace(min_free_gib=10**9, max_cache_gib=40)
        with self.assertRaises(w.Refused):
            w.capacity(self.paths, args)

    def fresh_build(self):
        paths = dict(self.paths)
        paths["target"] = self.base / "runs.noindex/fresh/linux-nix"
        paths["record"] = self.base / "records/fresh/linux-nix"
        paths.update({key: paths["target"] / key for key in ("work", "dist", "logs", "tests")})
        args = argparse.Namespace(command="build", target="linux-nix", run="fresh",
                                  min_free_gib=1, distribution=False, signing_config=None)
        return paths, args

    def test_invalid_jobs_leaves_no_unregistered_work(self):
        paths, args = self.fresh_build()
        with patch.object(package, "host_check"), patch.object(package, "tool_check"), \
                patch.object(runs, "capacity"), patch.object(common, "privacy"), \
                patch.object(common, "git_state", return_value=self.source):
            with self.assertRaises(w.Refused):
                w.build(args, paths, {"DESKPORT_JOBS": "invalid"})
        self.assertFalse(paths["target"].exists())
        self.assertFalse(paths["record"].exists())

    def test_all_linux_packages_record_archive_and_flatpak_before_freeze(self):
        paths, args = self.fresh_build()
        args.command, args.target, args.format, args.flatpak, args.asset_suffix = "package", "linux-portable", "all", True, "r2"
        version = (ROOT / "app/version.txt").read_text().strip()
        phases = []

        def command(command, actual_paths, env, phase):
            phases.append(phase)
            if phase == "build":
                paths["dist"].mkdir()
                appdir = paths["work"] / "DeskPort.AppDir"
                appdir.mkdir()
                (appdir / "LICENSE").write_text("retained upstream notice")
                (paths["work"] / "private.log").write_text("not a package input")
                for suffix in ("AppImage", "deb", "rpm", "pkg.tar.zst"):
                    (paths["dist"] / f"DeskPort-{version}.{suffix}").write_bytes(b"package")
            elif phase == "portable-archive":
                subprocess.run([str(value) for value in command], check=True,
                               env={**os.environ, "COPYFILE_DISABLE": "1"})
            elif phase == "flatpak":
                output = Path(command[-2])
                output.mkdir()
                (output / f"DeskPort-{version}-client-x86_64.flatpak").write_bytes(b"flatpak")

        with patch.object(package, "host_check"), patch.object(package, "tool_check"), \
                patch.object(runs, "capacity"), patch.object(common, "privacy"), \
                patch.object(common, "git_state", return_value=self.source), \
                patch.object(w.shutil, "which", return_value="flatpak"), \
                patch.object(package, "run_logged", side_effect=command), redirect_stdout(io.StringIO()):
            w.build(args, paths, {})
        record = w.read_record(paths, args)
        names = {asset["name"] for asset in record["assets"]}
        self.assertEqual(record["asset_suffix"], "r2")
        archive_name = f"DeskPort-{version}-linux-x86_64-r2.tar.gz"
        flatpak_name = f"DeskPort-{version}-client-x86_64-r2.flatpak"
        self.assertEqual(phases, ["build", "portable-archive", "flatpak"])
        self.assertIn(archive_name, names)
        self.assertIn(flatpak_name, names)
        self.assertFalse((paths["work"] / "flatpak" / f"DeskPort-{version}-client-x86_64.flatpak").exists())
        with tarfile.open(paths["dist"] / archive_name) as archive:
            self.assertEqual(set(archive.getnames()), {"DeskPort.AppDir", "DeskPort.AppDir/LICENSE"})
        checksums = (paths["dist"] / "SHA256SUMS-linux-portable-r2.txt").read_text()
        self.assertIn(archive_name, checksums)
        self.assertIn(flatpak_name, checksums)
        self.assertEqual(record["trees"]["dist"], w.footprint(paths["dist"]))
        self.assertFalse(any(w.source_material_archive(name) for name in names))

    def test_flatpak_requires_all_linux_portable_package_before_work_is_created(self):
        for arguments in (["package", "--target", "macos", "--format", "all"],
                          ["build", "--target", "linux-portable", "--format", "all"],
                          ["package", "--target", "linux-portable"]):
            with patch.object(runs, "layout") as layout, redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                w.main(arguments + ["--run", "invalid-flatpak", "--flatpak"])
            layout.assert_not_called()

    def test_linux_container_receives_explicit_cpu_limits_and_rejects_invalid_limits(self):
        fakebin = self.base / "fakebin"
        fakebin.mkdir()
        podman = fakebin / "podman"
        podman.write_text('#!/usr/bin/env python3\nimport json,sys\nprint(json.dumps(sys.argv[1:]))\n')
        podman.chmod(0o755)
        env = {**os.environ, "PATH": str(fakebin) + os.pathsep + os.environ["PATH"],
               "DESKPORT_BUILD_ROOT": str(self.base / "outputs"),
               "DESKPORT_LINUX_CPUSET": "0-3", "DESKPORT_LINUX_CPU_LIMIT": "2"}
        env.pop("DESKPORT_RUN_ID", None)
        command = ["bash", ROOT / "scripts/package-linux.sh"]
        arguments = json.loads(subprocess.check_output(command, env=env))
        self.assertEqual(arguments[arguments.index("--cpuset-cpus") + 1], "0-3")
        self.assertEqual(arguments[arguments.index("--cpus") + 1], "2")
        for key, value in (("DESKPORT_LINUX_CPUSET", "0-3;bad"), ("DESKPORT_LINUX_CPU_LIMIT", "0")):
            result = subprocess.run(command, env={**env, key: value}, capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")

    def fake_nix(self, stdout, stderr="", exit_code=0, delay=0):
        """Use real child IO/exit handling; never invoke an installed Nix."""
        paths, args = self.fresh_build()
        fakebin = self.base / "fakebin"
        fakebin.mkdir()
        fixture = self.base / "nix-fixture.json"
        fixture.write_text(json.dumps(dict(stdout=stdout, stderr=stderr, exit_code=exit_code, delay=delay)))
        nix = fakebin / "nix"
        nix.write_text(f'''#!{sys.executable}
import json,os,pathlib,sys,time
fixture = pathlib.Path(os.environ["DESKPORT_TEST_NIX_FIXTURE"])
data = json.loads(fixture.read_text())
fixture.with_name("nix-arguments.json").write_text(json.dumps(sys.argv[1:]))
fixture.with_name("nix-pid.txt").write_text(str(os.getpid()))
sys.stderr.write(data["stderr"])
sys.stderr.flush()
sys.stdout.write(data["stdout"])
sys.stdout.flush()
time.sleep(data["delay"])
sys.exit(data["exit_code"])
''')
        nix.chmod(0o755)
        env = {**os.environ, "PATH": str(fakebin) + os.pathsep + os.environ["PATH"],
               "DESKPORT_TEST_NIX_FIXTURE": str(fixture)}
        return paths, args, env

    def run_fake_nix(self, paths, args, env, *, existing=True, smoke_failure=False):
        # Only /nix/store directory availability is simulated. Argument creation,
        # child execution, stdout validation, logs and records all run normally.
        output = Path("/nix/store/" + "0" * 32 + "-deskport-test")
        is_dir = Path.is_dir
        self.nix_smoke_tmp = []
        logged = package.run_logged

        def command(command, actual_paths, child_env, phase, **kwargs):
            if phase != "smoke":
                return logged(command, actual_paths, child_env, phase, **kwargs)
            self.assertEqual(command, [sys.executable, "scripts/deskport-smoke.py", output])
            self.assertEqual(actual_paths, paths)
            self.assertEqual(json.loads((paths["record"] / "run.json").read_text())["nix_output"], str(output))
            temporary = Path(child_env["TMPDIR"])
            self.assertEqual(temporary.parent, paths["base"].parent / "t")
            (temporary / "fixture").write_text("test data")
            self.nix_smoke_tmp.append(temporary)
            if smoke_failure:
                raise w.Refused("fake smoke failure")

        with patch.object(package, "host_check"), patch.object(package, "tool_check"), \
                patch.object(runs, "capacity"), patch.object(common, "privacy"), \
                patch.object(common, "git_state", return_value=self.source), \
                patch.object(Path, "is_dir", lambda path: existing if path == output else is_dir(path)), \
                patch.object(package, "run_logged", side_effect=command), redirect_stdout(io.StringIO()):
            w.build(args, paths, env)

    def test_nix_stdout_record_and_smoke_without_store_links(self):
        output = "/nix/store/" + "0" * 32 + "-deskport-test"
        # More than a pipe buffer of stderr, including a plausible but unrelated
        # path. Mixed-log parsing and sequential pipe draining both fail here.
        stderr = "building dependency\n" * 8192 + "/nix/store/" + "1" * 32 + "-dependency\n"
        paths, args, env = self.fake_nix(output + "\n", stderr)
        self.run_fake_nix(paths, args, env)
        arguments = json.loads((self.base / "nix-arguments.json").read_text())
        self.assertEqual(arguments, ["build", "-L", "--cores", "4", "--max-jobs", "1",
                                     "--no-link", "--print-out-paths"])
        record = w.read_record(paths, args)
        self.assertEqual(record["status"], "built")
        self.assertFalse(record.get("failed", False))
        self.assertEqual(record["nix_output"], output)
        stdout_path = paths["record"] / "nix-outputs.txt"
        self.assertEqual(stdout_path.read_text(), output + "\n")
        self.assertEqual((paths["logs"] / "build.log").read_text(), stderr)
        step, = json.loads((paths["record"] / "steps.json").read_text())
        self.assertEqual(step["exit_code"], 0)
        self.assertEqual(step["stdout_sha256"], w.digest(stdout_path))
        self.assertEqual(step["log_sha256"], w.digest(paths["logs"] / "build.log"))
        self.assertFalse(any(p.is_symlink() for p in self.base.rglob("*")))
        self.assertEqual(len(self.nix_smoke_tmp), 1)
        self.assertFalse(self.nix_smoke_tmp[0].exists())
        self.assertFalse(paths["dist"].exists())
        args.work_only, args.apply = True, True
        with patch.object(common, "git_state", return_value=self.source), \
                patch.object(cleanup, "inactive"), redirect_stdout(io.StringIO()):
            w.clean(args, paths, record)
        self.assertFalse(paths["target"].exists())
        self.assertEqual(stdout_path.read_text(), output + "\n")
        self.assertEqual(w.read_record(paths, args)["nix_output"], output)

    def test_nix_nonzero_exit_never_accepts_valid_stdout(self):
        stdout = "/nix/store/" + "0" * 32 + "-deskport-test\n"
        paths, args, env = self.fake_nix(stdout, "build failed\n", 17)
        with self.assertRaisesRegex(w.Refused, r"build failed \(17\)"):
            self.run_fake_nix(paths, args, env)
        self.assertEqual(self.nix_smoke_tmp, [])
        record = w.read_record(paths, args)
        self.assertTrue(record["failed"])
        self.assertNotIn("nix_output", record)
        step, = json.loads((paths["record"] / "steps.json").read_text())
        self.assertEqual(step["exit_code"], 17)
        self.assertEqual((paths["record"] / "nix-outputs.txt").read_text(), stdout)
        self.assertEqual((paths["logs"] / "build.log").read_text(), "build failed\n")

    def test_nix_rejects_empty_multiple_or_invalid_stdout(self):
        output = "/nix/store/" + "0" * 32 + "-deskport-test"
        for stdout in ("", "\n", output + "\n" + output + "\n", output + "\n" + output + "-other\n", output + "\n\n",
                       "building...\n" + output + "\n", output + "/bin\n",
                       output + "/../escape\n", "/tmp/deskport\n",
                       "/nix/store/invalid-hash-deskport\n", output + "é\n"):
            with self.subTest(stdout=stdout), tempfile.TemporaryDirectory(dir=self.base) as temporary:
                original_base = self.base
                self.base = Path(temporary)
                try:
                    # Even a valid path on stderr must never rescue stdout.
                    paths, args, env = self.fake_nix(stdout, output + "\n")
                    with self.assertRaises(w.Refused):
                        self.run_fake_nix(paths, args, env)
                    self.assertEqual(self.nix_smoke_tmp, [])
                    record = w.read_record(paths, args)
                    self.assertTrue(record["failed"])
                    self.assertNotIn("nix_output", record)
                    step, = json.loads((paths["record"] / "steps.json").read_text())
                    self.assertEqual(step["exit_code"], 0)
                finally:
                    self.base = original_base

    def test_nix_rejects_missing_store_directory(self):
        paths, args, env = self.fake_nix("/nix/store/" + "0" * 32 + "-deskport-test\n")
        with self.assertRaisesRegex(w.Refused, "existing store directory"):
            self.run_fake_nix(paths, args, env, existing=False)
        self.assertEqual(self.nix_smoke_tmp, [])
        self.assertTrue(w.read_record(paths, args)["failed"])

    def test_nix_stdout_capture_preserves_space_guard_and_stops_child(self):
        paths, args, env = self.fake_nix("", "waiting\n", delay=60)
        with patch.object(package.shutil, "disk_usage", return_value=types.SimpleNamespace(free=0)), \
                self.assertRaisesRegex(w.Refused, "free-space reserve"):
            self.run_fake_nix(paths, args, env)
        pid = int((self.base / "nix-pid.txt").read_text())
        with self.assertRaises(ProcessLookupError):
            os.kill(pid, 0)
        self.assertEqual(self.nix_smoke_tmp, [])
        self.assertTrue(w.read_record(paths, args)["failed"])
        self.assertEqual((paths["logs"] / "build.log").read_text(), "waiting\n")

    def test_run_logged_existing_api_keeps_combined_log(self):
        command = [sys.executable, "-c",
                   "import sys; print('stdout', flush=True); print('stderr', file=sys.stderr, flush=True)"]
        with redirect_stdout(io.StringIO()):
            w.run_logged(command, self.paths, os.environ.copy(), "compatibility")
        self.assertEqual((self.paths["logs"] / "compatibility.log").read_text(), "stdout\nstderr\n")
        step, = json.loads((self.paths["record"] / "steps.json").read_text())
        self.assertEqual(step["exit_code"], 0)
        self.assertNotIn("stdout_path", step)

    def test_nix_smoke_failure_removes_short_tmp_and_retains_output_record(self):
        output = "/nix/store/" + "0" * 32 + "-deskport-test"
        paths, args, env = self.fake_nix(output + "\n")
        with self.assertRaisesRegex(w.Refused, "fake smoke failure"):
            self.run_fake_nix(paths, args, env, smoke_failure=True)
        self.assertEqual(len(self.nix_smoke_tmp), 1)
        self.assertFalse(self.nix_smoke_tmp[0].exists())
        record = w.read_record(paths, args)
        self.assertTrue(record["failed"])
        self.assertEqual(record["nix_output"], output)

    def test_legacy_nix_result_link_cleanup_preserves_store_contents(self):
        # A legacy run fixture points at a local fake store, never /nix/store.
        store = self.base / "store-result"
        store.mkdir()
        (store / "application").write_bytes(b"retained Nix result")
        (self.paths["work"] / "result").symlink_to(store)
        w.freeze(self.paths, self.record)
        self.args.work_only = True
        with patch.object(common, "git_state", return_value=self.source), \
                patch.object(cleanup, "inactive"), redirect_stdout(io.StringIO()):
            w.clean(self.args, self.paths, self.record)
        self.assertFalse(self.paths["work"].exists())
        self.assertEqual((store / "application").read_bytes(), b"retained Nix result")

    def test_process_level_github_upload_download_and_cleanup(self):
        """Exercise gh upload/server-digest protocol without package downloads."""
        fakebin = self.base / "fakebin"
        remote = self.base / "github"
        fakebin.mkdir()
        remote.mkdir()
        gh = fakebin / "gh"
        gh.write_text('''#!/usr/bin/env python3
import hashlib,json,os,pathlib,shutil,sys
root=pathlib.Path(os.environ['DESKPORT_TEST_GITHUB'])
args=sys.argv[1:]
files=sorted(root.iterdir())
if args[:2]==['release','upload']:
    source=pathlib.Path(args[3]); shutil.copyfile(source,root/source.name)
elif args[0]=='api' and '/releases/tags/' in args[1]:
    print(json.dumps({'id':1,'draft':False,'assets':[
      {'id':i+1,'name':p.name,'size':p.stat().st_size,'state':'uploaded',
       'digest':'sha256:'+hashlib.sha256(p.read_bytes()).hexdigest(),
       'browser_download_url':'https://example.invalid/'+p.name}
      for i,p in enumerate(files)]}))
elif args[0]=='api' and '/releases/assets/' in args[1]:
    sys.exit(19)  # Default verification must never download installer bytes.
else: sys.exit(9)
''')
        gh.chmod(0o755)
        self.args.target = "windows"
        self.record.update(status="built", asset_suffix="r2")
        with patch.object(common, "git_state", return_value=self.source):
            w.finish_package(self.args, self.paths, {}, self.record)
        w.freeze(self.paths, self.record)
        with patch.dict(os.environ, {"PATH": str(fakebin) + os.pathsep + os.environ["PATH"], "DESKPORT_TEST_GITHUB": str(remote)}), \
                patch.object(common, "privacy"), patch.object(common, "git_state", return_value=self.source), \
                patch.object(cleanup, "inactive"), redirect_stdout(io.StringIO()):
            w.publish(self.args, self.paths, self.record)
            receipt = json.loads((self.paths["record"] / "github-receipt.json").read_text())
            self.assertEqual(len(receipt["assets"]), 3)
            self.assertTrue(all("-r2." in item["name"] for item in receipt["assets"]))
            w.clean(self.args, self.paths, self.record)
        self.assertFalse(self.paths["target"].exists())
        self.assertEqual(len(list(remote.iterdir())), 3)
        self.assertTrue((self.paths["cache"] / "library.a").exists())


if __name__ == "__main__":
    unittest.main()
