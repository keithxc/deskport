#!/usr/bin/env python3
"""Negative controls for acceptance tools; no desktop, network or personal data."""
import array
import importlib.util
import json
import math
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import wave

spec = importlib.util.spec_from_file_location("regression", Path(__file__).resolve().parents[1] / "scripts/pc-regression.py")
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)


class AcceptanceTools(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="pc-regression-")
        cls.root = Path(cls.temporary.name)
        cls.fixture = cls.root / "fixture"
        r.fixture(cls.fixture)
        cls.manifest = cls.fixture / "fixture.json"

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def copy(self):
        target = self.root / self._testMethodName
        shutil.copytree(self.fixture / "payload", target)
        return target

    def capture(self, transform):
        rate, data = r.pcm(self.fixture / "stereo.wav")
        data = transform(data)
        if sys.byteorder != "little": data.byteswap()
        path = self.root / (self._testMethodName + ".wav")
        with wave.open(str(path), "wb") as audio:
            audio.setparams((2, 2, rate, 0, "NONE", "not compressed"))
            audio.writeframes(data.tobytes())
        return r.audio_check(self.fixture / "stereo.wav", path)

    def test_deterministic_fixture(self):
        other = self.root / "repeat"
        r.fixture(other)
        self.assertEqual(r.digest(self.manifest), r.digest(other / "fixture.json"))
        with self.assertRaises(FileExistsError): r.fixture(other)

    def test_copy_bytes_and_empty_directories(self):
        self.assertEqual(r.verify_copy(self.manifest, self.copy())["status"], "pass")

    def test_corrupt_byte(self):
        target = self.copy()
        (target / "files/chunks.bin").write_bytes(b"wrong")
        with self.assertRaises(ValueError): r.verify_copy(self.manifest, target)

    def test_missing_empty_directory(self):
        target = self.copy()
        (target / "files/folder with spaces/empty directory").rmdir()
        with self.assertRaises(ValueError): r.verify_copy(self.manifest, target)

    def test_extra_file(self):
        target = self.copy()
        (target / "unexpected").touch()
        with self.assertRaises(ValueError): r.verify_copy(self.manifest, target)

    def test_symlink(self):
        target = self.copy()
        (target / "link").symlink_to(target / "files")
        with self.assertRaises(ValueError): r.verify_copy(self.manifest, target)

    def test_audio_gain_and_delay(self):
        result = self.capture(lambda d: array.array("h", [0] * 48000 + [int(v * 0.5) for v in d]))
        self.assertEqual(result["status"], "pass", result)
        self.assertAlmostEqual(result["offset_seconds"], 0.5, delta=0.04)

    def test_audio_silence(self):
        self.assertEqual(self.capture(lambda d: array.array("h", [0] * len(d)))["status"], "fail")

    def test_audio_swapped_channels(self):
        def swap(d):
            for i in range(0, len(d), 2): d[i], d[i + 1] = d[i + 1], d[i]
            return d
        self.assertEqual(self.capture(swap)["status"], "fail")

    def test_audio_mono_duplication(self):
        def mono(d):
            for i in range(0, len(d), 2): d[i + 1] = d[i]
            return d
        self.assertEqual(self.capture(mono)["status"], "fail")

    def test_audio_channel_imbalance(self):
        def unbalance(d):
            for i in range(1, len(d), 2): d[i] = int(d[i] * 0.1)
            return d
        self.assertEqual(self.capture(unbalance)["status"], "fail")

    def test_audio_truncation(self):
        with self.assertRaises(ValueError): self.capture(lambda d: d[:48000])

    def test_audio_wrong_tone_same_envelope(self):
        def wrong(d):
            for i in range(0, len(d), 2):
                t = i / 2 / 48000
                gate = 0.35 + 0.65 * math.sin(math.pi * (t % 1)) ** 2
                for c in (0, 1):
                    active = int(t) in ((1, 3, 5, 7) if c == 0 else (2, 3, 6, 7))
                    d[i + c] = round(8192 * gate * math.sin(2 * math.pi * 220 * t)) if active else 0
            return d
        self.assertEqual(self.capture(wrong)["status"], "fail")

    def observation(self, layer="hardware"):
        endpoint = dict(alias="synthetic", os="synthetic", device="synthetic",
                        installed_version="fixture", source_sha="a" * 40,
                        core_sha="e" * 40,
                        package_sha256="b" * 64, binary_sha256="c" * 64)
        return dict(direction=r.DIRECTIONS[0], case=r.CASES[0], layer=layer, status="pass",
                    fixture_sha256=r.digest(self.manifest), run_id="fixture-run",
                    identity=dict(client=endpoint.copy(), host=endpoint.copy()),
                    observed_at="2026-10-17T12:00:00Z", method="synthetic negative control",
                    expected="fixture", actual="fixture",
                    evidence=[dict(path=str(self.manifest), sha256=r.digest(self.manifest))])

    def ledger(self, observation):
        path = self.root / (self._testMethodName + ".json")
        note = self.root / (self._testMethodName + "-note.json")
        r.matrix_init(path, self.manifest)
        r.write_json(note, observation)
        return path, note

    def test_isolated_never_counts_as_hardware(self):
        path, note = self.ledger(self.observation("isolated"))
        r.matrix_record(path, note)
        report = r.matrix_summary(r.read_json(path), "fixture-run")
        self.assertEqual(report["hardware"][r.DIRECTIONS[0]]["untested"], len(r.CASES))

    def test_historical_run_never_counts(self):
        path, note = self.ledger(self.observation())
        r.matrix_record(path, note)
        report = r.matrix_summary(r.read_json(path), "new-package")
        self.assertFalse(report["complete"])
        self.assertEqual(report["hardware"][r.DIRECTIONS[0]]["pass"], 0)

    def test_latest_hardware_failure_supersedes_pass(self):
        observation = self.observation()
        path, note = self.ledger(observation)
        r.matrix_record(path, note)
        observation["status"] = "fail"
        r.write_json(note, observation)
        report = r.matrix_record(path, note)
        self.assertEqual(report["hardware"][r.DIRECTIONS[0]]["pass"], 0)
        self.assertEqual(report["hardware"][r.DIRECTIONS[0]]["fail"], 1)

    def test_changed_install_rejected(self):
        observation = self.observation()
        path, note = self.ledger(observation)
        r.matrix_record(path, note)
        observation["identity"]["client"]["binary_sha256"] = "d" * 64
        r.write_json(note, observation)
        with self.assertRaisesRegex(ValueError, "identity changed"): r.matrix_record(path, note)

    def test_evidence_tampering_rejected(self):
        observation = self.observation()
        observation["evidence"][0]["sha256"] = "0" * 64
        path, note = self.ledger(observation)
        with self.assertRaises(ValueError): r.matrix_record(path, note)

    def test_fixture_mismatch_rejected(self):
        observation = self.observation()
        observation["fixture_sha256"] = "0" * 64
        path, note = self.ledger(observation)
        with self.assertRaises(ValueError): r.matrix_record(path, note)

    def test_report_rechecks_hardware_evidence(self):
        observation = self.observation()
        evidence = self.root / "mutable-evidence.txt"
        evidence.write_text("synthetic observation")
        observation["evidence"] = [dict(path=str(evidence), sha256=r.digest(evidence))]
        path, note = self.ledger(observation)
        r.matrix_record(path, note)
        evidence.write_text("changed")
        with self.assertRaises(ValueError): r.matrix_summary(r.read_json(path), "fixture-run")


if __name__ == "__main__":
    unittest.main()
