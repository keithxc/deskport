#!/usr/bin/env python3
"""Synthetic PC acceptance fixtures and evidence ledger. No network/clipboard access."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import wave
import zlib

DIRECTIONS = ("kde-amd-to-macos", "macos-to-kde-amd", "windows-to-macos", "macos-to-windows")
CASES = ("video-motion", "audio-stereo", "audio-route", "keys-shortcuts",
         "scroll-vertical", "scroll-horizontal", "trackpad", "hide-release",
         "focus-release", "disconnect", "reconnect", "network-recovery",
         "clipboard-text", "clipboard-image", "clipboard-files", "clipboard-directory",
         "clipboard-cancel", "display-restore", "dpi-restore")
LAYERS = ("isolated", "vm", "hardware")


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def write_json(path, value):
    # All generated files belong in an explicitly selected, private build path.
    _build_storage.output(path).write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def png():
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    # Distinct corners expose rotation, cropping and channel swaps.
    colors = ((255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0))
    pixels = b"".join(b"\0" + b"".join(bytes(colors[(y >= 32) * 2 + (x >= 32)])
                                        for x in range(64)) for y in range(64))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 64, 64, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))


def fixture(output):
    root = _build_storage.output(output)
    root.mkdir(parents=True, exist_ok=False, mode=0o700)
    payload = root / "payload"
    payload.mkdir()
    files = {
        "text.txt": "DeskPort synthetic clipboard\nEnglish 中文 العربية 한국어 😀\nline 2\tend\n".encode(),
        "corners.png": png(),
        "files/empty.bin": b"",
        "files/chunks.bin": bytes(range(256)) * 2051,
        "files/folder with spaces/中文.txt": "Synthetic nested file\n".encode(),
        "files/folder with spaces/.hidden": b"synthetic hidden file\n",
    }
    for name, data in files.items():
        path = payload / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    (payload / "files/folder with spaces/empty directory").mkdir()
    rate = 48000
    samples = array.array("h")
    # 1 s silence; L-only, R-only, both, silence, L-only, R-only, both.
    # Distinct frequencies and a time-varying envelope expose duplication/swaps.
    for i in range(rate * 8):
        t = i / rate
        phase = int(t)
        gate = (0.35 + 0.65 * math.sin(math.pi * (t % 1)) ** 2)
        for channel, freq in enumerate((440, 880)):
            active = phase in ((1, 3, 5, 7) if channel == 0 else (2, 3, 6, 7))
            samples.append(round(8192 * gate * math.sin(2 * math.pi * freq * t)) if active else 0)
    if sys.byteorder != "little":
        samples.byteswap()
    with wave.open(str(root / "stereo.wav"), "wb") as audio:
        audio.setparams((2, 2, rate, 0, "NONE", "not compressed"))
        audio.writeframes(samples.tobytes())
    template = Path(__file__).resolve().parents[1] / "tests/pc-regression.html"
    (root / "index.html").write_bytes(template.read_bytes())
    manifest = {"schema": 1, "kind": "synthetic-pc-regression",
                "files": {name: {"bytes": len(data), "sha256": digest(payload / name)}
                          for name, data in files.items()},
                "directories": sorted(str(p.relative_to(payload)) for p in payload.rglob("*") if p.is_dir()),
                "audio": {"rate": rate, "channels": 2, "seconds": 8,
                          "sha256": digest(root / "stereo.wav")},
                "page_sha256": digest(root / "index.html")}
    write_json(root / "fixture.json", manifest)
    return {"fixture_sha256": digest(root / "fixture.json"), "output": str(root)}


def verify_copy(manifest_path, destination):
    manifest = read_json(manifest_path)
    root = Path(destination).resolve()
    expected = manifest["files"]
    found = {}
    directories = []
    # Walk without following symlinks; reject them, including directory links.
    for path in root.rglob("*"):
        if path.is_symlink():
            raise ValueError("Copied payload contains a symlink")
        name = str(path.relative_to(root))
        if path.is_dir():
            directories.append(name)
        elif path.is_file():
            found[name] = {"bytes": path.stat().st_size, "sha256": digest(path)}
        else:
            raise ValueError("Copied payload contains a special file")
    if found != expected or sorted(directories) != manifest["directories"]:
        raise ValueError("Copied payload differs: file bytes/names or directory inventory")
    return {"status": "pass", "files": len(found), "directories": len(directories),
            "scope": "payload bytes and names only; transport not established"}


def pcm(path):
    with wave.open(str(path), "rb") as audio:
        if audio.getsampwidth() != 2 or audio.getnchannels() != 2 or audio.getcomptype() != "NONE":
            raise ValueError("Expected uncompressed stereo signed 16-bit WAV")
        rate = audio.getframerate()
        data = array.array("h", audio.readframes(audio.getnframes()))
    if sys.byteorder != "little":
        data.byteswap()
    return rate, data


def audio_check(reference_path, capture_path):
    rate, reference = pcm(reference_path)
    capture_rate, capture = pcm(capture_path)
    if rate != capture_rate:
        raise ValueError("Resample the capture explicitly to the reference sample rate")
    # Compare at 1 kHz for bounded work. Search up to 2 seconds of leading
    # capture silence; the recording must contain the complete reference.
    step = max(1, rate // 1000)
    ref = [[reference[i + c] / 32768 for i in range(0, len(reference) - 1, step * 2)] for c in (0, 1)]
    cap = [[capture[i + c] / 32768 for i in range(0, len(capture) - 1, step * 2)] for c in (0, 1)]
    # Envelope energy, rather than waveform phase, tolerates codec sample delay.
    def envelope(values):
        return [math.sqrt(sum(v * v for v in values[i:i + 20]) / 20)
                for i in range(0, len(values) - 19, 20)]
    ref = list(map(envelope, ref))
    cap = list(map(envelope, cap))
    count = len(ref[0])
    if count < 200 or len(cap[0]) < count:
        raise ValueError("Capture is shorter than the complete reference")
    def similarity(a, b):
        energy = sum(x * x for x in a) * sum(y * y for y in b)
        return sum(x * y for x, y in zip(a, b)) / math.sqrt(energy) if energy else 0
    best = None
    for offset in range(min(100, len(cap[0]) - count) + 1):
        channels = [cap[c][offset:offset + count] for c in (0, 1)]
        correlations = [similarity(ref[c], channels[c]) for c in (0, 1)]
        score = min(correlations)
        if best is None or score > best[0]:
            best = score, offset, channels, correlations
    score, offset, channels, correlations = best
    # Check energy in intended silent windows separately: envelope similarity
    # alone could accept a duplicated mono channel or continuous background tone.
    leakage = []
    rms = []
    gains = []
    for c in (0, 1):
        active = [i for i, v in enumerate(ref[c]) if v > 0.01]
        silent = [i for i, v in enumerate(ref[c]) if v < 0.001]
        if not active or not silent:
            raise ValueError("Reference lacks active/silent channel windows")
        on = sum(channels[c][i] ** 2 for i in active) / len(active)
        off = sum(channels[c][i] ** 2 for i in silent) / len(silent)
        leakage.append(math.sqrt(off / on) if on else 1)
        rms.append(math.sqrt(on))
        gains.append(math.sqrt(on / (sum(ref[c][i] ** 2 for i in active) / len(active))))
    balance = max(gains) / min(gains) if min(gains) else None
    tone_fraction = []
    for channel, frequency in enumerate((440, 880)):
        # Goertzel energy in a 200 ms channel-only window rejects unrelated
        # tones/noise that happen to have the same volume envelope.
        begin = round((offset * 0.02 + 1.2 + channel) * rate)
        values = capture[begin * 2 + channel:(begin + rate // 5) * 2:2]
        coefficient = 2 * math.cos(2 * math.pi * frequency / rate)
        previous = older = 0.0
        for sample in values:
            current = sample + coefficient * previous - older
            older, previous = previous, current
        power = previous * previous + older * older - coefficient * previous * older
        energy = sum(v * v for v in values)
        tone_fraction.append(max(0, 2 * power / (len(values) * energy)) if energy else 0)
    passed = (score >= 0.96 and max(leakage) < 0.10 and min(rms) > 0.005
              and balance is not None and balance < 1.5 and min(tone_fraction) > 0.8)
    return {"status": "pass" if passed else "fail", "envelope_similarity": correlations,
            "silent_to_active_rms": leakage, "active_rms": rms, "channel_gain_ratio": balance,
            "expected_tone_energy_fraction": tone_fraction,
            "offset_seconds": offset * 0.02,
            "scope": "stereo sequence only; listening, output route, latency and video sync require separate evidence"}


def identity(value):
    # Aliases only; no addresses, credentials, host config or clipboard data.
    for endpoint in ("client", "host"):
        item = value[endpoint]
        for key in ("alias", "os", "device", "installed_version", "source_sha", "core_sha", "package_sha256", "binary_sha256"):
            if not isinstance(item.get(key), str) or not item[key].strip():
                raise ValueError(f"Missing {endpoint}.{key}")
        for key, length in (("source_sha", 40), ("core_sha", 40), ("package_sha256", 64), ("binary_sha256", 64)):
            if len(item[key]) != length or any(c not in "0123456789abcdef" for c in item[key]):
                raise ValueError(f"Invalid {endpoint}.{key}")
    return value


def matrix_init(path, fixture_path):
    target = Path(path)
    if target.exists():
        raise ValueError("Refusing to overwrite an existing ledger")
    value = {"schema": 1, "fixture_sha256": digest(fixture_path), "runs": [],
             "directions": {d: {c: [] for c in CASES} for d in DIRECTIONS}}
    write_json(target, value)
    return matrix_summary(value)


def matrix_record(path, observation_path):
    ledger = read_json(path)
    observation = read_json(observation_path)
    direction, case = observation["direction"], observation["case"]
    if direction not in DIRECTIONS or case not in CASES:
        raise ValueError("Unknown direction or case")
    if observation["layer"] not in LAYERS or observation["status"] not in ("pass", "fail", "blocked"):
        raise ValueError("Invalid layer or status")
    if observation["fixture_sha256"] != ledger["fixture_sha256"]:
        raise ValueError("Fixture identity differs")
    identity(observation["identity"])
    for key in ("run_id", "observed_at", "method", "expected", "actual"):
        if not isinstance(observation.get(key), str) or not observation[key].strip():
            raise ValueError(f"Missing {key}")
    # A run's identity cannot be silently changed after recording one case.
    run = {"run_id": observation["run_id"], "direction": direction, "identity": observation["identity"]}
    previous = [r for r in ledger["runs"] if r["run_id"] == run["run_id"] and r["direction"] == direction]
    if previous and previous[0] != run:
        raise ValueError("Run endpoint identity changed; create a new run")
    evidence = observation.get("evidence", [])
    if not evidence:
        raise ValueError("An observation needs concrete evidence files")
    for item in evidence:
        source = (Path(observation_path).resolve().parent / item["path"]).resolve()
        if not source.is_file() or digest(source) != item["sha256"]:
            raise ValueError("Evidence is missing or hash differs")
        # Retain a resolvable path when ledger and observation directories differ.
        item["path"] = str(source)
    if not previous:
        ledger["runs"].append(run)
    ledger["directions"][direction][case].append(observation)
    write_json(path, ledger)
    return matrix_summary(ledger, observation["run_id"])


def matrix_summary(value, run_id=None):
    result = {}
    for direction, cases in value["directions"].items():
        counts = {"pass": 0, "fail": 0, "blocked": 0, "untested": 0}
        for observations in cases.values():
            # Never promote VM/isolated tests into physical acceptance. A selected
            # run never inherits any earlier package's passing observation.
            eligible = [o for o in observations if o["layer"] == "hardware" and o["run_id"] == run_id]
            if eligible:
                for item in eligible[-1]["evidence"]:
                    if digest(item["path"]) != item["sha256"]:
                        raise ValueError("Recorded hardware evidence changed")
            counts[eligible[-1]["status"] if eligible else "untested"] += 1
        result[direction] = counts
    return {"run_id": run_id, "hardware": result,
            "complete": bool(run_id) and all(v["pass"] == len(CASES) for v in result.values())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("fixture"); p.add_argument("output")
    p = commands.add_parser("verify-copy"); p.add_argument("manifest"); p.add_argument("destination")
    p = commands.add_parser("audio"); p.add_argument("reference"); p.add_argument("capture")
    p = commands.add_parser("matrix-init"); p.add_argument("ledger"); p.add_argument("fixture")
    p = commands.add_parser("matrix-record"); p.add_argument("ledger"); p.add_argument("observation")
    p = commands.add_parser("matrix-report"); p.add_argument("ledger"); p.add_argument("--run-id", required=True)
    args = parser.parse_args()
    try:
        if args.command == "fixture": result = fixture(args.output)
        elif args.command == "verify-copy": result = verify_copy(args.manifest, args.destination)
        elif args.command == "audio": result = audio_check(args.reference, args.capture)
        elif args.command == "matrix-init": result = matrix_init(args.ledger, args.fixture)
        elif args.command == "matrix-record": result = matrix_record(args.ledger, args.observation)
        else: result = matrix_summary(read_json(args.ledger), args.run_id)
        print(json.dumps(result, indent=2))
        return 1 if result.get("status") == "fail" else 0
    except (ValueError, KeyError, OSError, wave.Error) as error:
        parser.exit(2, f"{error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
