#!/usr/bin/env python3
"""Exercise the source privacy guard with synthetic material and local Git only."""
from __future__ import annotations

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()


import importlib.util
import io
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import tarfile
import zipfile

sys.dont_write_bytecode = True


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/check-build-privacy.py"
spec = importlib.util.spec_from_file_location("build_privacy", SCRIPT)
privacy = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = privacy
spec.loader.exec_module(privacy)
team = "Z9Y8X7W6V5"
fingerprint = "a1" * 20
identity = "Developer ID " + "Application: Synthetic Sensitive Fixture (" + team + ")"
marker = "-----BEGIN " + "PRIVATE KEY-----"
team_name = "DESKPORT_SIGN_" + "TEAM"
identity_name = "DESKPORT_SIGN_" + "IDENTITY"
tests = 0


def check(condition, description):
    global tests
    if not condition:
        raise AssertionError(description)  # Never include a payload in assertions.
    tests += 1


for value in (team, '"' + team + '"', "'" + team + "'"):
    check(bool(privacy.scan_text(team_name + "=" + value, "build.sh", "build.sh")),
          "literal team assignment must fail")
for text in (
    "export " + identity_name + "='" + identity + "'",
    identity_name + "=" + fingerprint,
    identity_name + '="${LOCAL_IDENTITY:-' + fingerprint + '}"',
    "codesign --sign " + fingerprint + " app",
    'codesign --sign "' + fingerprint + '" app',
    "notarytool --team-id " + team,
    'notarytool --team-id "' + team + '"',
    marker,
):
    check(bool(privacy.scan_text(text, "release.txt", "release.txt")),
          "synthetic private signing material must fail")
for text in (
    team_name + "=APPLE_TEAM_ID",
    identity_name + "=DEVELOPER_ID_CERTIFICATE_SHA1",
    identity_name + "='your local signing identity'",
    identity_name + '="$LOCAL_IDENTITY"',
    identity_name + '="${LOCAL_IDENTITY}"',
    ': "${' + identity_name + ':?Set a local identity}"',
    'if [ "$' + identity_name + '" = - ]; then',
    'CODE_SIGN_IDENTITY = "Apple Development";',
    "-----BEGIN CERTIFICATE-----\nPUBLIC_FIXTURE\n-----END CERTIFICATE-----",
    'token = secrets.token_hex(24)',
):
    check(not privacy.scan_text(text, "build.sh", "build.sh"),
          "placeholders, code references, and public certificates must pass")
fixture = (ROOT / "tests/diagnostics-test.cpp").read_text()
check(not privacy.scan_text(fixture, "tests/diagnostics-test.cpp", "fixture"),
      "known exact redaction fixture must pass")
check(bool(privacy.scan_text(fixture, "tests/new-test.cpp", "fixture")),
      "a test directory must not grant a blanket exception")
check(bool(privacy.scan_text(fixture.replace(marker, marker + " altered"),
                            "tests/diagnostics-test.cpp", "fixture")),
      "changing the allowlisted line must require review")

# Read the public fixtures from their pinned archive without printing key bytes.
vendor = ROOT / "host/vendor/sunshine.tar.gz"
vendor_manifest = json.loads((ROOT / "host/vendor/sunshine.json").read_text())
vendor_hash = hashlib.sha256()
with vendor.open("rb") as stream:
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        vendor_hash.update(block)
check(vendor_hash.hexdigest() == vendor_manifest["sha256"] == privacy.UPSTREAM_FIXTURE_ARCHIVE_SHA256,
      "upstream fixture archive must match both the vendor pin and reviewed digest")
upstream_fixtures = {}
with tarfile.open(vendor) as archive:
    for name, expected in privacy.UPSTREAM_MARKER_FIXTURES.items():
        data = archive.extractfile(name).read()
        upstream_fixtures[name] = data
        check(hashlib.sha256(data).hexdigest() == expected["sha256"],
              "upstream fixture file must match its reviewed complete digest")
        for number, line_hash in expected["markers"].items():
            check(hashlib.sha256(data.splitlines()[number - 1]).hexdigest() == line_hash,
                  "upstream fixture marker must match its reviewed line digest")
        for relative in (name, "source/Sunshine/" + name):
            check(not privacy.scan_stream(io.BytesIO(data), relative, "fixture"),
                  "only exact reviewed upstream fixture content may pass")
        check(bool(privacy.scan_stream(io.BytesIO(data), "tests/unit/unreviewed.cpp", "fixture")),
              "the reviewed content under an unknown filename must fail")
        lines = data.splitlines(keepends=True)
        next_line = min(expected["markers"])
        lines[next_line] = (b"X" if lines[next_line][:1] != b"X" else b"Y") + lines[next_line][1:]
        changed_body = b"".join(lines)
        check(bool(privacy.scan_stream(io.BytesIO(changed_body), name, "fixture")),
              "replacing only a PEM body must not inherit a marker-line exception")
        check(bool(privacy.scan_stream(io.BytesIO(data + b"\n"), name, "fixture")),
              "any change to a reviewed upstream fixture must require review")
        check(bool(privacy.scan_stream(io.BytesIO(data + marker.encode()), name, "fixture")),
              "an additional private marker in a known fixture must fail")
for filename, ignored in (("local.p12", True), ("local.pfx", True), ("local.p8", True),
                          ("local.keychain-db", True), ("config/build-signing.env", True),
                          ("config/build-signing.env.local", True), ("notary-result.json", True),
                          ("app-submit.json", True), ("certificate.pem", False),
                          ("certificate.crt", False), ("certificate.cer", False),
                          ("public.pub", False), ("LICENSE", False),
                          ("config/build-signing.env.example", False)):
    result = subprocess.run(["git", "-C", str(ROOT), "check-ignore", "--quiet", "--no-index", filename])
    check((result.returncode == 0) == ignored, "Git ignore rules must hide only private material")

base = Path(subprocess.check_output(["bash", str(ROOT / "scripts/build-paths.sh"), "checkout"],
                                    text=True).strip()) / "privacy-tests.noindex"
base.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="guard-", dir=base) as temporary:
    root = Path(temporary)
    repo = root / "repo"
    repo.mkdir()
    for filename in ("certificate.pem", "certificate.crt", "certificate.cer", "public.pub"):
        (repo / filename).write_text("-----BEGIN CERTIFICATE-----\nPUBLIC_FIXTURE\n")
        check(not privacy.private_filename(Path(filename)), "public certificate filename must pass")
    for filename in ("private.p12", "private.pfx", "private.p8", "local.keychain-db",
                     "local.mobileprovision", "build-signing.env", "signing.env.local",
                     "build-signing.local.env", ".env.local", "notary-result.json", "app-log.json"):
        check(bool(privacy.private_filename(Path(filename))), "private filename must fail")
    (repo / "safe.txt").write_text("public source\n")
    (repo / "binary.dat").write_bytes(b"\0" + identity.encode())
    check(not privacy.scan_file(repo, "binary.dat", "binary.dat")[0],
          "binary upstream signing provenance must pass")
    (repo / "binary-key.dat").write_bytes(b"\0" + marker.encode())
    check(bool(privacy.scan_file(repo, "binary-key.dat", "binary-key.dat")[0]),
          "binary data must not hide a private key marker")
    (repo / "binary-key.dat").unlink()
    (repo / "utf16.txt").write_text(team_name + "=" + team, encoding="utf-16")
    check(bool(privacy.scan_file(repo, "utf16.txt", "utf16.txt")[0]),
          "UTF-16 settings must be checked")
    (repo / "utf16.txt").unlink()
    (repo / "internal-link").symlink_to("safe.txt")
    check(not privacy.scan_file(repo, "internal-link", "internal-link")[0],
          "internal source symlink must pass")
    outside = root / "private.env"
    outside.write_text("synthetic external content")
    (repo / "external-link").symlink_to(outside)
    check(bool(privacy.scan_file(repo, "external-link", "external-link")[0]),
          "external symlink must fail without reading its target")
    (repo / "external-link").unlink()

    def git(*args):
        return subprocess.run(["git", "-C", str(repo), *args], check=True,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def run(*args):
        return subprocess.run([sys.executable, str(SCRIPT), *args], capture_output=True, text=True)

    git("init", "--quiet")
    git("add", ".")
    (repo / "untracked.txt").write_text(identity)
    result = run("--repo", str(repo))
    check(result.returncode == 0 and result.stdout == "" and result.stderr == "",
          "default scan must inspect tracked files only and remain silent when clean")
    result = run("--source-dir", str(repo))
    check(result.returncode == 1, "explicit source scan must include untracked material")
    check(identity not in result.stdout + result.stderr and team not in result.stdout + result.stderr,
          "privacy output must not echo sensitive values")
    check("untracked.txt:1: personal signing identity literal" in result.stdout,
          "privacy output must identify only location and fixed reason")
    git("add", "untracked.txt")
    check(run("--repo", str(repo)).returncode == 1, "staged source content must be scanned")
    (repo / "untracked.txt").unlink()
    check(run("--repo", str(repo)).returncode == 2, "missing tracked files must fail closed")
    check(run("--source-dir", str(root / "missing")).returncode == 2,
          "missing source trees must fail closed")
    (repo / "untracked.txt").write_text("safe")
    unusual = "bad\nname.env"
    (repo / unusual).write_text(identity)
    result = run("--source-dir", str(repo))
    check("bad\\nname.env" in result.stdout and len(result.stdout.splitlines()) == 1,
          "filenames must not inject extra output lines")

    def tar(path, entries):
        with tarfile.open(path, "w:gz") as archive:
            for name, data, target in entries:
                member = tarfile.TarInfo(name)
                if target is not None:
                    member.type = tarfile.SYMTYPE
                    member.linkname = target
                    archive.addfile(member)
                else:
                    member.size = len(data)
                    archive.addfile(member, io.BytesIO(data))

    good = root / "good.tar.gz"
    tar(good, [("source/source.txt", b"public source", None),
               ("source/link", b"", "source.txt"),
               ("source/Framework/Versions/A/Resources/info.txt", b"public", None),
               ("source/Framework/Versions/Current", b"", "A"),
               ("source/Framework/Resources", b"", "Versions/Current/Resources"),
               ("source/tests/diagnostics-test.cpp", fixture.encode(), None)])
    result = run("--archive", str(good))
    check(result.returncode == 0 and result.stdout == "", "safe tar source and framework links must pass")
    upstream = root / "upstream.tar.gz"
    tar(upstream, [("source/Sunshine/" + name, data, None)
                   for name, data in upstream_fixtures.items()])
    result = run("--archive", str(upstream))
    check(result.returncode == 0 and result.stdout == "", "exact upstream fixtures must pass archive scanning")
    changed_upstream = root / "changed-upstream.tar.gz"
    tar(changed_upstream, [("source/Sunshine/" + name, data + b"\n", None)
                           for name, data in upstream_fixtures.items()])
    result = run("--archive", str(changed_upstream))
    check(result.returncode == 1, "changed upstream fixtures must fail archive scanning")
    for name, data in upstream_fixtures.items():
        first_payload_line = data.splitlines()[min(privacy.UPSTREAM_MARKER_FIXTURES[name]["markers"])]
        check(first_payload_line not in (result.stdout + result.stderr).encode(),
              "upstream fixture rejection must not print private fixture payloads")
    for index, (name, data, target) in enumerate((
        ("source/private.txt", identity.encode(), None),
        ("source/config/build-signing.env", b"synthetic", None),
        ("../escape", b"public", None),
        ("/absolute", b"public", None),
        ("source/link", b"", "../../outside"),
        ("source/link", b"", "missing"),
        ("source/key.bin", b"\0" * 65525 + marker.encode(), None),
        ("source/long.txt", b"x" * 300000 + b"\n" + identity.encode(), None),
    )):
        bad = root / f"bad-{index}.tar.gz"
        tar(bad, [(name, data, target)])
        result = run("--archive", str(bad))
        check(result.returncode == 1, "unsafe tar source material must fail")
        check(identity not in result.stdout + result.stderr and marker not in result.stdout + result.stderr,
              "archive diagnostics must not include private content")
    duplicate = root / "duplicate.tar.gz"
    tar(duplicate, [("source/a", b"a", None), ("source/a", b"b", None)])
    check(run("--archive", str(duplicate)).returncode == 1, "duplicate archive members must fail")
    cycle = root / "cycle.tar.gz"
    tar(cycle, [("source/a", b"", "b"), ("source/b", b"", "a")])
    check(run("--archive", str(cycle)).returncode == 1, "cyclic archive links must fail")
    for private in (False, True):
        zipped = root / ("bad.zip" if private else "good.zip")
        with zipfile.ZipFile(zipped, "w") as archive:
            archive.writestr("source/file.txt", identity if private else "public source")
        result = run("--archive", str(zipped))
        check(result.returncode == int(private), "zip source members must use the same privacy rules")
        check(identity not in result.stdout + result.stderr, "zip diagnostics must not echo contents")
    zipped = root / "bad-link.zip"
    with zipfile.ZipFile(zipped, "w") as archive:
        entry = zipfile.ZipInfo("source/link")
        entry.create_system = 3
        entry.external_attr = 0o120777 << 16
        archive.writestr(entry, "../../outside")
    check(run("--archive", str(zipped)).returncode == 1, "zip symlink escapes must fail")
    corrupt = root / "corrupt.tar.gz"
    corrupt.write_bytes(b"invalid archive")
    check(run("--archive", str(corrupt)).returncode == 2, "invalid archives must fail closed")

print(f"PASS: {tests} privacy checks; synthetic values were not printed")
