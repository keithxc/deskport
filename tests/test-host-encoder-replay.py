#!/usr/bin/env python3
"""Compile an offline replay of the production macOS asynchronous encoder.

The full host overlay series is applied first. The complete encode_run(),
encode_avcodec(), nv12_zero_device::convert() and deskport_set_bitrate()
definitions are extracted
verbatim, with SHA-256 provenance, and compiled against the pinned host FFmpeg.
Only image/mailbox construction, session factory and the packet sink are test
adapters. No display, input, network, deployed app or host service is used.

This covers production scheduling, conversion ownership, FFmpeg submission,
completion, drop numbering and refinement, not ScreenCaptureKit, the encoder
factory, SPS rewriting, packetization, transport or client presentation.
"""
from pathlib import Path
import argparse
import hashlib
import json
import signal
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def definition(source, signature):
    """Read a complete C++ definition without counting braces in comments/text."""
    if source.count(signature) != 1:
        raise ValueError(f"Expected one production definition: {signature}")
    start = source.index(signature)
    brace = source.index("{", start)
    depth, state, escaped, i = 0, "code", False, brace
    while i < len(source):
        char, following = source[i], source[i:i + 2]
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "comment":
            if following == "*/":
                state, i = "code", i + 1
        elif state in ('"', "'"):
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == state:
                state = "code"
        elif following in ("//", "/*"):
            state, i = ("line" if following == "//" else "comment"), i + 1
        elif char in ('"', "'"):
            state = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
        i += 1
    raise ValueError(f"Unterminated production definition: {signature}")


def sha(text):
    return hashlib.sha256(text.encode()).hexdigest()


def publish_source_record(source_record, output_prefix):
    """Place the exact compiled record beside packets without overwriting it."""
    contents = source_record.read_bytes()
    json.loads(contents)
    target = output_prefix.resolve().parent / "replay-source.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        with target.open("xb") as stream:
            stream.write(contents)
    except FileExistsError:
        if target.is_symlink() or target.read_bytes() != contents:
            raise ValueError(f"Conflicting replay source record: {target}")
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--ffmpeg-root", type=Path)
    parser.add_argument("--emit-only", action="store_true")
    parser.add_argument("--source-tree", type=Path, help="Already fully overlaid host source; otherwise prepare a fresh tree")
    parser.add_argument("--scenes-list", type=Path)
    parser.add_argument("--events", type=Path)
    parser.add_argument("--output-prefix", type=Path)
    parser.add_argument("--width", type=int, default=2040)
    parser.add_argument("--height", type=int, default=1228)
    parser.add_argument("--fps", type=int, default=60)
    parser.add_argument("--kbps", type=int, default=1359)
    parser.add_argument("--negotiated-kbps", type=int, help="Factory bitrate before applying the live saver cap; defaults to --kbps")
    parser.add_argument("--ceiling-permille", type=int, help="Exact production launch ceiling, bypassing cap-to-permille rounding")
    parser.add_argument("--burst", type=int, default=3, help="Scenario burst hint; the extracted production setter controls the actual property")
    parser.add_argument("--duration-ms", type=int, default=20000)
    args = parser.parse_args()
    work = args.work_dir.resolve()
    if work.is_relative_to(ROOT):
        parser.error("Replay output must be outside the source checkout")
    work.mkdir(parents=True, exist_ok=True)
    source = args.source_tree.resolve() if args.source_tree else work / "host-source"
    if not args.source_tree:
        subprocess.run([sys.executable, str(ROOT / "scripts/prepare-host-source.py"), str(source)], check=True)
        subprocess.run([sys.executable, str(ROOT / "scripts/apply-host-patches.py"), "--platform", "macos", str(source)], check=True)
    video = (source / "src/video.cpp").read_text()
    conversion = (source / "src/platform/macos/nv12_zero_device.cpp").read_text()
    blocks = {
        "ASYNC_ENCODER": definition(video, "  void encode_run("),
        "AVCODEC_ENCODER": definition(video, "  int encode_avcodec("),
        "NATIVE_CONVERSION": definition(conversion, "  int nv12_zero_device::convert("),
        "RATE_SETTER": definition(video, "    bool deskport_set_bitrate(int kbps, bool constrained, bool saver) override"),
    }
    template = ROOT / "tests/host-encoder-replay.cpp.in"
    generated = template.read_text()
    for name, content in blocks.items():
        marker = "/* PRODUCTION_" + name + " */"
        if generated.count(marker) != 1:
            raise ValueError(f"Expected one template marker: {marker}")
        generated = generated.replace(marker, content)
    cpp = work / "host-encoder-replay.cpp"
    cpp.write_text(generated)
    headers = {}
    names = ["common/smartstream.h", "common/inputactivity.h", "common/framecadence.h", "common/encoderpolicy.h",
             "common/ratecontrol.h", "macos/vtbitrate.h", "macos/vtlatency.h"]
    if (source / "src/deskport/macos/vtclock.h").exists():
        names.append("macos/vtclock.h")
    if (source / "src/deskport/common/picturequality.h").exists():
        names.append("common/picturequality.h")
    for name in names:
        contents = (source / "src/deskport" / name).read_bytes()
        target = work / "include/host" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(contents)
        headers["host/" + name] = hashlib.sha256(contents).hexdigest()
    provenance = {
        "source_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
        "video_cpp_sha256": sha(video),
        "native_conversion_cpp_sha256": sha(conversion),
        "template_sha256": sha(template.read_text()),
        "generated_sha256": sha(generated),
        "blocks_sha256": {name: sha(content) for name, content in blocks.items()},
        "headers_sha256": headers,
        "coverage": ["production encode_run", "production encode_avcodec", "production nv12_zero_device::convert", "production deskport_set_bitrate", "real steady_clock", "real pinned FFmpeg and VideoToolbox"],
        "adapters": ["session factory", "input images", "mailboxes", "packet sink", "disabled SPS rewriting"],
    }
    (work / "replay-source.json").write_text(json.dumps(provenance, indent=2) + "\n")
    if args.emit_only:
        print(cpp)
        return
    if sys.platform != "darwin" or not args.ffmpeg_root:
        parser.error("Compilation requires macOS and --ffmpeg-root")
    ffmpeg = args.ffmpeg_root.resolve()
    binary = work / "host-encoder-replay"
    command = ["xcrun", "clang++", "-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I", str(work / "include"), "-I", str(ffmpeg / "include"), "-L", str(ffmpeg / "lib"),
               str(cpp), "-lavcodec", "-lavutil", "-lSvtAv1Enc", "-lx264", "-lx265", "-lz", "-lbz2", "-liconv", "-pthread"]
    for framework in ("VideoToolbox", "CoreFoundation", "CoreMedia", "CoreVideo", "CoreServices", "Security"):
        command += ["-framework", framework]
    subprocess.run(command + ["-o", str(binary)], check=True)
    subprocess.run(["codesign", "--force", "--sign", "-", "--options", "runtime", str(binary)], check=True)
    print(binary, flush=True)
    if args.scenes_list:
        if not args.events or not args.output_prefix:
            parser.error("Replay requires --events and --output-prefix")
        if args.output_prefix.resolve().is_relative_to(ROOT):
            parser.error("Replay output must be outside the source checkout")
        publish_source_record(work / "replay-source.json", args.output_prefix)
        command = [str(binary), str(args.width), str(args.height), str(args.fps), str(args.kbps), str(args.burst),
                   str(args.duration_ms), str(args.scenes_list.resolve()), str(args.events.resolve()), str(args.output_prefix.resolve())]
        if args.negotiated_kbps is not None or args.ceiling_permille is not None:
            command.append(str(args.negotiated_kbps if args.negotiated_kbps is not None else args.kbps))
        if args.ceiling_permille is not None:
            command.append(str(args.ceiling_permille))
        subprocess.run(command, check=True)


if __name__ == "__main__":
    def terminate(number, _frame):
        # subprocess.run kills/reaps its direct compiler or native child when
        # this exception unwinds; the orchestrator also owns their full group.
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        raise SystemExit(128 + number)

    signal.signal(signal.SIGTERM, terminate)
    main()
