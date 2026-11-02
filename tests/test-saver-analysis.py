#!/usr/bin/env python3
"""Software-only quality/budget oracle checks; no VideoToolbox or GUI.

Requires NumPy. The real editor/typed-image check additionally requires Pillow,
software FFmpeg and Menlo or DejaVu Sans Mono; it skips if these are unavailable.
Temporary resources use DESKPORT_TEST_DIR or this checkout's build/tests path.
"""
import contextlib
import copy
import csv
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
test_override = os.environ.get("DESKPORT_TEST_DIR")
if test_override:
    task_TMPDIR = Path(test_override).expanduser().resolve()
else:
    checkout_build = subprocess.run(["bash", str(ROOT / "scripts/build-paths.sh"), "checkout"],
                                    check=True, capture_output=True, text=True).stdout.strip()
    if not checkout_build:
        raise RuntimeError("Build-path resolver returned no checkout directory")
    task_TMPDIR = Path(checkout_build) / "tests" / "saver-analysis"
task_TMPDIR.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location("quality_oracle", ROOT / "tests/test-vt-data-saver.py")
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


class Decoder:
    def __init__(self, data):
        self.stdout = io.BytesIO(data)

    def wait(self):
        return 0

    def poll(self):
        return 0


class QualityOracle(unittest.TestCase):
    def fixture(self, directory, *, output_us=10000, decoded=None, empty=False, outputs=None):
        width = height = 64
        reference = np.zeros((height, width), dtype=np.uint8)
        reference[10, 10] = 255  # A tiny new glyph must not disappear in a large flat ROI.
        mask = np.zeros_like(reference)
        mask[10, 10] = 255
        nv12, text_mask = directory / "scene.nv12", directory / "mask.gray"
        nv12.write_bytes(reference.tobytes() + bytes([128]) * (width * height // 2))
        text_mask.write_bytes(mask.tobytes())
        scenario = {"width": width, "height": height, "durationMs": 1000,
                    "events": [{"atMs": 0, "scene": "new", "verify": True}],
                    "quality": {"textPsnrDb": 30, "psnrDb": 35, "maxRecoveryMs": 750}}
        resources = {"new": {"nv12": str(nv12), "textMask": str(text_mask), "roi": [0, 0, width, height]}}
        prefix = directory / "fixture.v1"  # The native producer appends suffixes; it does not replace .v1.
        if outputs is None:
            outputs = [] if empty else [(output_us, decoded is not None)]
        oracle.artifact(prefix, ".hevc").write_bytes(b"x" * len(outputs))
        fields = ["wire_index", "input", "event", "scene", "native_pts", "packet_pts", "bytes", "output_us", "capture_us"]
        with oracle.artifact(prefix, ".packets.csv").open("w") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            for index, (timestamp, _) in enumerate(outputs):
                writer.writerow(dict(zip(fields, [index, index, 0, "new", index, index, 1, timestamp, 0])))
        decoded_bytes = b"".join((reference if good else np.zeros_like(reference)).tobytes() for _, good in outputs)
        return self.evaluate(scenario, resources, prefix, decoded_bytes)

    def evaluate(self, scenario, resources, prefix, decoded):
        with patch.object(subprocess, "Popen", return_value=Decoder(decoded)) as decoder, \
             patch.object(subprocess, "run", return_value=subprocess.CompletedProcess([], 0, stdout="ffmpeg software-oracle-test\n")), \
             contextlib.redirect_stdout(io.StringIO()):
            passed = oracle.analyze(scenario, resources, prefix, "test-decoder")
        report = json.loads(oracle.artifact(prefix, ".report.json").read_text())
        self.assertFalse((prefix.parent / "fixture.report.json").exists())
        return passed, report, decoder.call_count

    def test_stale_tiny_edit_fails_even_when_roi_average_passes(self):
        with tempfile.TemporaryDirectory(prefix="saver-oracle-", dir=task_TMPDIR) as tmp:
            passed, report, _ = self.fixture(Path(tmp))
        self.assertFalse(passed)
        self.assertGreater(report["events"][0]["firstPsnr"], 35)
        self.assertEqual(report["events"][0]["firstTextPsnr"], 0)
        self.assertTrue(report["deliveryPass"])
        self.assertFalse(report["qualityPass"])
        # Preserve the actual resource regression too: the old editor must not
        # pass for the newly typed line, despite most of the desktop matching.
        ffmpeg = shutil.which("ffmpeg")
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("actual editor check requires Pillow")
        fonts = (Path('/System/Library/Fonts/Menlo.ttc'),
                 Path('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf'))
        if not ffmpeg or not any(path.is_file() for path in fonts):
            self.skipTest("actual editor check requires software FFmpeg and a supported font")
        generator_spec = importlib.util.spec_from_file_location("scene_generator", ROOT / "tests/generate-saver-scenes.py")
        generator = importlib.util.module_from_spec(generator_spec)
        generator_spec.loader.exec_module(generator)
        with tempfile.TemporaryDirectory(prefix="saver-editor-oracle-", dir=task_TMPDIR) as tmp:
            directory = Path(tmp)
            with contextlib.redirect_stdout(io.StringIO()):
                generator.generate(directory / "assets", 2040, 1228, 47)
            scenario = oracle.load_scenario(directory / "assets/timeline.json")
            (directory / "input").mkdir()
            resources, _, _ = oracle.prepare(scenario, directory / "input", ffmpeg)
            scenario["durationMs"] = 2000
            scenario["events"] = [{"atMs": 0, "scene": "editor-dark", "verify": False},
                                  {"atMs": 1000, "scene": "editor-typed", "verify": True}]
            prefix = directory / "fixture.v1"
            oracle.artifact(prefix, ".hevc").write_bytes(b"x")
            with oracle.artifact(prefix, ".packets.csv").open("w") as stream:
                writer = csv.DictWriter(stream, fieldnames=["wire_index", "input", "event", "scene", "native_pts", "packet_pts", "bytes", "output_us", "capture_us"])
                writer.writeheader()
                writer.writerow({"wire_index": 0, "input": 0, "event": 1, "scene": "editor-typed",
                    "native_pts": 0, "packet_pts": 0, "bytes": 1, "output_us": 1010000, "capture_us": 1000000})
            stale = Path(resources["editor-dark"]["nv12"]).read_bytes()[:2040 * 1228]
            passed, report, _ = self.evaluate(scenario, resources, prefix, stale)
            self.assertFalse(passed)
            self.assertLess(report["events"][1]["firstTextPsnr"], 30)
            print(f"PASS stale editor rejected for typed line: text_psnr={report['events'][1]['firstTextPsnr']:.3f} dB")

    def test_empty_output_is_quality_failure_not_decoder_error(self):
        with tempfile.TemporaryDirectory(prefix="saver-oracle-", dir=task_TMPDIR) as tmp:
            passed, report, calls = self.fixture(Path(tmp), empty=True)
        self.assertFalse(passed)
        self.assertEqual(calls, 0)
        self.assertEqual(report["events"][0]["outputs"], 0)
        self.assertIsNone(report["events"][0]["recoveryMs"])
        self.assertFalse(report["deliveryPass"])
        self.assertEqual(report["missingVerifiedOutputEvents"], 1)

    def test_good_but_late_output_fails_real_deadline(self):
        with tempfile.TemporaryDirectory(prefix="saver-oracle-", dir=task_TMPDIR) as tmp:
            passed, report, _ = self.fixture(Path(tmp), output_us=800000, decoded="reference")
        self.assertFalse(passed)
        self.assertEqual(report["events"][0]["recoveryMs"], 800)
        self.assertFalse(report["deliveryPass"])
        self.assertEqual(report["lateVerifiedOutputEvents"], 1)

    def test_good_timely_output_passes(self):
        with tempfile.TemporaryDirectory(prefix="saver-oracle-", dir=task_TMPDIR) as tmp:
            passed, report, _ = self.fixture(Path(tmp), decoded="reference")
        self.assertTrue(passed)
        self.assertEqual(report["events"][0]["recoveryMs"], 10)

    def test_early_clear_frame_cannot_hide_later_blur(self):
        with tempfile.TemporaryDirectory(prefix="saver-oracle-", dir=task_TMPDIR) as tmp:
            passed, report, _ = self.fixture(Path(tmp), outputs=[(10000, True), (900000, False)])
        self.assertFalse(passed)
        self.assertEqual(report["events"][0]["firstGoodMs"], 10)
        self.assertIsNone(report["events"][0]["recoveryMs"])
        self.assertFalse(report["qualityStableAfterRecovery"])
        with tempfile.TemporaryDirectory(prefix="saver-oracle-", dir=task_TMPDIR) as tmp:
            passed, report, _ = self.fixture(Path(tmp), outputs=[(10000, True), (100000, False), (600000, True)])
        self.assertTrue(passed)
        self.assertEqual(report["events"][0]["firstGoodMs"], 10)
        self.assertEqual(report["events"][0]["recoveryMs"], 600)
        self.assertTrue(report["qualityStableAfterRecovery"])

    def test_network_and_runtime_mismatches_fail_closed(self):
        scenario = {"network": {"packetSize": 896, "fecPercentage": 20, "acceptanceMinParity": 2}}
        network, minimum = oracle.resolve_network(scenario)
        self.assertEqual((network["packet_size"], network["fec_percentage"], minimum), (896, 20, 2))
        alternate = copy.deepcopy(scenario)
        alternate["network"].update(packetSize=512, fecPercentage=40, ipVersion=6, encrypted=True)
        network, _ = oracle.resolve_network(alternate)
        self.assertEqual(network, {"packet_size": 512, "fec_percentage": 40, "ip_version": 6, "encrypted": True})
        for bad in ({"network": {"unsupported": 1}}, {"network": {"acceptanceMinParity": 1}}):
            with self.assertRaises(ValueError):
                oracle.resolve_network(bad)
        with self.assertRaises(ValueError):
            oracle.resolve_network(scenario, 512)
        with self.assertRaises(ValueError):
            oracle.validate_runtime({"bitrateKbps": 992}, {"effectiveCapKbps": 499})

    def test_missing_output_time_and_double_frame_header_are_rejected(self):
        scenario = {"events": [{"atMs": 0, "scene": "new"}]}
        row = {"wire_index": 0, "event": 0, "scene": "new", "bytes": 100, "output_us": 10}
        with self.assertRaises(ValueError):
            oracle.validate_packets(scenario, [row], 108)
        row.pop("output_us")
        with self.assertRaises(ValueError):
            oracle.validate_packets(scenario, [row], 100)


if __name__ == "__main__":
    unittest.main()
