#!/usr/bin/env python3
"""Decode actual ANSI/half-block CLI output, including its quiet zone."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import argparse
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import zlib

parser = argparse.ArgumentParser()
parser.add_argument("--input", required=True, type=Path)
parser.add_argument("--work-dir", required=True, type=Path)
args = parser.parse_args()
args.work_dir.mkdir(parents=True, exist_ok=True)
lines = args.input.read_text().splitlines()
expected = next(line for line in lines if line.startswith("deskport://bind?"))
rows = []
modules = {" ": (255, 255), "▀": (0, 255), "▄": (255, 0), "█": (0, 0)}
for line in lines:
    if not line.startswith("\x1b[30;107m"):
        continue
    assert line.endswith("\x1b[0m")
    cells = line.removeprefix("\x1b[30;107m").removesuffix("\x1b[0m")
    rows.extend([[modules[cell][i] for cell in cells] for i in (0, 1)])
assert rows and all(len(row) == len(rows[0]) for row in rows)
assert len(rows) == len(rows[0]) + 1
assert all(all(pixel == 255 for pixel in row) for row in rows[:4] + rows[-4:])
assert all(all(pixel == 255 for pixel in row[:4] + row[-4:]) for row in rows)
scale = 8
width, height = len(rows[0]) * scale, len(rows) * scale
pixels = b"".join((b"\x00" + bytes(pixel for pixel in row for _ in range(scale))) * scale for row in rows)
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
png = args.work_dir / "terminal-qr.png"
png.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))
if sys.platform == "darwin":
    decoder = args.work_dir / "qr-decode"
    source = Path(__file__).resolve().parents[1] / "tests/qr-decode.swift"
    subprocess.run(["/usr/bin/swiftc", "-module-cache-path", str(args.work_dir / "swift-cache"), str(source), "-o", str(decoder)], check=True)
    decoded = subprocess.check_output([str(decoder), str(png)], text=True).strip()
else:
    decoder = shutil.which("zbarimg")
    if not decoder:
        raise SystemExit("Install test-only zbarimg to decode the terminal QR")
    decoded = subprocess.check_output([decoder, "--quiet", "--raw", str(png)], text=True).strip()
assert decoded == expected, "Decoded terminal QR did not match the exact invitation URI"
print("PASS: actual terminal QR decoded to its exact URI; explicit colors and quiet zone verified")
