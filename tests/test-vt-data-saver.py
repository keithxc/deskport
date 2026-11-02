#!/usr/bin/env python3
"""Resource-driven VideoToolbox quality regression; no screen capture or input.

The default runner uses production FrameCadence, VT rate/completion adapters and
the pinned host FFmpeg. It covers native codec/common-policy behavior, separately
from the macOS asynchronous capture/mailbox/conversion loop. --analyze-prefix
accepts the same packet provenance from the production asynchronous replay.
"""
import argparse
import csv
import hashlib
import json
import math
import pathlib
import re
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True


ROOT = pathlib.Path(__file__).resolve().parents[1]


def require(ok, message):
    if not ok:
        raise ValueError(message)


def artifact(prefix, suffix):
    return prefix.with_name(prefix.name + suffix)


def resolve_network(scenario, packet_size_override=None):
    from saver_wire import estimate_frame
    network = scenario.get("network", {})
    allowed = {"packetSize", "fecPercentage", "acceptanceMinParity", "ipVersion", "encrypted"}
    require(isinstance(network, dict) and not set(network) - allowed, "unsupported network configuration")
    packet_size = network.get("packetSize", 896)
    if packet_size_override is not None:
        require("packetSize" not in network or packet_size_override == packet_size,
                "CLI packet-size conflicts with timeline network.packetSize")
        packet_size = packet_size_override
    result = {"packet_size": packet_size, "fec_percentage": network.get("fecPercentage", 20),
              "ip_version": network.get("ipVersion", 4), "encrypted": network.get("encrypted", False)}
    minimum = network.get("acceptanceMinParity", 2)
    require(type(minimum) is int and 2 <= minimum <= 255, "acceptanceMinParity must retain at least two parity shards")
    require(type(result["encrypted"]) is bool, "network.encrypted must be boolean")
    require(type(result["ip_version"]) is int, "network.ipVersion must be an integer")
    estimate_frame(1, min_parity=minimum, **result)
    return result, minimum


def validate_runtime(scenario, runtime):
    if runtime is None:
        return
    for expected, actual in (("bitrateKbps", "effectiveCapKbps"),
                             ("encoderNegotiatedKbps", "negotiatedKbps"),
                             ("ceilingPermille", "ceilingPermille")):
        if expected in scenario:
            require(runtime.get(actual) == scenario[expected], f"encoder runtime {actual} conflicts with timeline {expected}")


def validate_packets(scenario, packets, elementary_size):
    require(sum(int(row["bytes"]) for row in packets) == elementary_size,
            "packet byte counts differ from elementary stream; frame header must not be counted twice")
    previous_output = -1
    for index, packet in enumerate(packets):
        require(int(packet["wire_index"]) == index, "wire frame sequence is not contiguous")
        if index and packet.get("wire_pts") is not None:
            require(int(packet["wire_pts"]) == int(packets[index - 1]["wire_pts"]) + 1,
                    "production wire PTS is not contiguous")
        event_index = int(packet["event"])
        require(0 <= event_index < len(scenario["events"]), "packet event is outside the timeline")
        event = scenario["events"][event_index]
        require(packet["scene"] == event["scene"], "packet source scene/event mismatch")
        require(int(packet["bytes"]) > 0, "packet has no encoded bytes")
        require(packet.get("output_us") not in (None, ""), "packet has no real output timestamp")
        output_us = int(packet["output_us"])
        require(output_us >= previous_output and output_us >= event["atMs"] * 1000,
                "packet output timestamp precedes its event or an earlier output")
        if packet.get("capture_us") not in (None, ""):
            require(event["atMs"] * 1000 <= int(packet["capture_us"]) <= output_us,
                    "packet capture/output timestamps do not match its source event")
        previous_output = output_us


def load_scenario(path):
    scenario = json.loads(path.read_text())
    require(scenario.get("version") == 1, "unsupported timeline version")
    require(scenario.get("codec", "hevc_videotoolbox") == "hevc_videotoolbox", "this fixture currently requires HEVC VideoToolbox")
    for name in ("width", "height", "durationMs"):
        require(isinstance(scenario.get(name), int) and scenario[name] > 0, f"invalid {name}")
    require(scenario["width"] % 4 == 0 and scenario["height"] % 4 == 0, "dimensions must be multiples of four")
    ids = set()
    for scene in scenario["scenes"]:
        require(re.fullmatch(r"[A-Za-z0-9_.-]+", scene["id"]), "unsafe scene ID")
        require(scene["id"] not in ids, "duplicate scene ID")
        ids.add(scene["id"])
        scene["source"] = str((path.parent / scene["path"]).resolve())
        require(pathlib.Path(scene["source"]).is_file(), f"missing PNG: {scene['source']}")
        if scene.get("textMaskPath"):
            scene["maskSource"] = str((path.parent / scene["textMaskPath"]).resolve())
            require(pathlib.Path(scene["maskSource"]).is_file(), "missing text mask")
        scene.setdefault("roi", [0, 0, scenario["width"], scenario["height"]])
        x, y, w, h = scene["roi"]
        require(x >= 0 and y >= 0 and w > 0 and h > 0 and x + w <= scenario["width"] and y + h <= scenario["height"], "invalid scene ROI")
        if scene.get("backgroundRoi"):
            bx, by, bw, bh = scene["backgroundRoi"]
            require(bx >= 0 and by >= 0 and bw > 0 and bh > 0 and bx + bw <= scenario["width"] and by + bh <= scenario["height"],
                    "invalid background ROI")
    last = -1
    for event in scenario["events"]:
        require(isinstance(event["atMs"], int) and last < event["atMs"] < scenario["durationMs"], "event times must increase within duration")
        require(event["scene"] in ids, "event references unknown scene")
        last = event["atMs"]
    require(scenario["events"] and scenario["events"][0]["atMs"] == 0, "first event must be at zero")
    resolve_network(scenario)
    for key in ("psnrDb", "textPsnrDb", "maxRecoveryMs", "maxProjectedMBPerHour"):
        value = scenario.get("quality", {}).get(key)
        if value is not None:
            require(type(value) in (int, float) and math.isfinite(value) and value > 0, f"invalid quality.{key}")
    return scenario


def prepare(scenario, output, ffmpeg):
    cache = output / "nv12"
    cache.mkdir(exist_ok=True)
    width, height = scenario["width"], scenario["height"]
    resources = {}
    scene_lines = []
    for index, scene in enumerate(scenario["scenes"]):
        source = pathlib.Path(scene["source"])
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        require(not scene.get("sha256") or scene["sha256"] == digest, "scene PNG differs from declared checksum")
        nv12 = cache / f"{scene['id']}-{digest[:16]}.nv12"
        if not nv12.exists():
            result = subprocess.run([ffmpeg, "-hide_banner", "-nostdin", "-loglevel", "error", "-i", str(source),
                "-vf", f"scale={width}:{height}:in_range=full:out_range=tv:out_color_matrix=bt709,format=nv12",
                "-frames:v", "1", "-f", "rawvideo", "pipe:1"], capture_output=True, check=True)
            require(len(result.stdout) == width * height * 3 // 2, "converted NV12 size mismatch")
            nv12.write_bytes(result.stdout)
        require(nv12.stat().st_size == width * height * 3 // 2, "cached NV12 size mismatch")
        scene_lines.append(f"{scene['id']}\t{nv12}\n")
        resources[scene["id"]] = {"index": index, "source": str(source), "sourceSha256": digest,
            "nv12": str(nv12), "nv12Sha256": hashlib.sha256(nv12.read_bytes()).hexdigest(), "roi": scene["roi"]}
        if scene.get("maskSource"):
            mask_source = pathlib.Path(scene["maskSource"])
            mask_digest = hashlib.sha256(mask_source.read_bytes()).hexdigest()
            mask = cache / f"{scene['id']}-mask-{mask_digest[:16]}.gray"
            if not mask.exists():
                result = subprocess.run([ffmpeg, "-hide_banner", "-nostdin", "-loglevel", "error", "-i", str(mask_source),
                    "-vf", f"scale={width}:{height}:flags=neighbor,format=gray", "-frames:v", "1", "-f", "rawvideo", "pipe:1"],
                    capture_output=True, check=True)
                require(len(result.stdout) == width * height, "converted mask size mismatch")
                mask.write_bytes(result.stdout)
            require(mask.stat().st_size == width * height, "cached text mask size mismatch")
            resources[scene["id"]].update({"textMask": str(mask), "textMaskSource": str(mask_source),
                "textMaskSourceSha256": mask_digest, "textMaskSha256": hashlib.sha256(mask.read_bytes()).hexdigest()})
        if scene.get("backgroundRoi"):
            resources[scene["id"]]["backgroundRoi"] = scene["backgroundRoi"]
    scenes_file = output / "scenes.list"
    events_file = output / "events.tsv"
    scenes_file.write_text("".join(scene_lines))
    events_file.write_text("".join(f"{event['atMs'] * 1000}\t{resources[event['scene']]['index']}\t"
        f"{int(event.get('activity', True))}\t{int(event.get('recovery', False))}\n" for event in scenario["events"]))
    (output / "resources.json").write_text(json.dumps(resources, indent=2) + "\n")
    return resources, scenes_file, events_file


def compile_runner(ffmpeg_root, output):
    binary = output / "vt-data-saver"
    command = ["xcrun", "clang++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
        "-I", str(ffmpeg_root / "include"), str(ROOT / "tests/vt-data-saver.cpp"), "-L", str(ffmpeg_root / "lib"),
        "-lavcodec", "-lavutil", "-lSvtAv1Enc", "-lx264", "-lx265", "-lz", "-lbz2", "-liconv", "-pthread"]
    for framework in ("VideoToolbox", "CoreFoundation", "CoreMedia", "CoreVideo", "CoreServices", "Security"):
        command += ["-framework", framework]
    subprocess.run(command + ["-o", str(binary)], check=True)
    subprocess.run(["codesign", "--force", "--sign", "-", "--options", "runtime", str(binary)], check=True)
    return binary


def psnr(mse):
    return 120.0 if mse == 0 else 10 * math.log10(255 * 255 / mse)


def analyze(scenario, resources, prefix, ffmpeg, compare_events=(), packet_size=None):
    import numpy as np
    width, height = scenario["width"], scenario["height"]
    with artifact(prefix, ".packets.csv").open(newline="") as stream:
        reader = csv.DictReader(stream)
        packet_columns = reader.fieldnames or []
        require({"wire_index", "input", "event", "scene", "native_pts", "packet_pts", "bytes", "output_us"} <= set(packet_columns),
                "packet provenance CSV is missing required columns")
        packets = list(reader)
    validate_packets(scenario, packets, artifact(prefix, ".hevc").stat().st_size)
    runtime_record = artifact(prefix, ".runtime.json")
    runtime = json.loads(runtime_record.read_text()) if runtime_record.exists() else None
    validate_runtime(scenario, runtime)
    references = {key: np.memmap(value["nv12"], dtype=np.uint8, mode="r", shape=(height * 3 // 2, width))[:height]
                  for key, value in resources.items()}
    masks = {key: np.memmap(value["textMask"], dtype=np.uint8, mode="r", shape=(height, width)) > 0
             for key, value in resources.items() if "textMask" in value}
    comparisons, snapshots = {}, {}
    for event_index in compare_events:
        event = scenario["events"][event_index]
        samples = [row for row in packets if int(row["event"]) == event_index]
        if not samples:
            comparisons[event_index] = []
            continue
        selected = [("first", samples[0])]
        target_us = event["atMs"] * 1000 + 3000000
        end_us = (scenario["events"][event_index + 1]["atMs"] if event_index + 1 < len(scenario["events"]) else scenario["durationMs"]) * 1000
        if target_us < end_us:
            displayed = [row for row in packets if int(row["output_us"]) <= target_us]
            if displayed:
                selected.append(("display-at-3s", displayed[-1]))
        comparisons[event_index] = selected
    selected_wires = {int(row["wire_index"]) for selected in comparisons.values() for _, row in selected}
    metrics = []
    with artifact(prefix, ".decode.log").open("wb") as log:
        decoder = subprocess.Popen([ffmpeg, "-hide_banner", "-nostdin", "-threads", "1", "-i", str(artifact(prefix, ".hevc")),
            "-vf", "extractplanes=y", "-vsync", "0", "-f", "rawvideo", "-pix_fmt", "gray", "pipe:1"], stdout=subprocess.PIPE, stderr=log) if packets else None
        try:
            for packet in packets:
                raw = decoder.stdout.read(width * height)
                require(len(raw) == width * height, "decoder output count differs from packet provenance")
                image = np.frombuffer(raw, dtype=np.uint8).reshape(height, width)
                if int(packet["wire_index"]) in selected_wires:
                    snapshots[int(packet["wire_index"])] = image.copy()
                reference = references[packet["scene"]]
                x, y, w, h = resources[packet["scene"]]["roi"]
                delta = image[y:y+h, x:x+w].astype(np.float64) - reference[y:y+h, x:x+w]
                row = dict(packet)
                row["roi_psnr"] = psnr(float(np.mean(delta * delta)))
                mask = masks.get(packet["scene"])
                row["text_psnr"] = None
                if mask is not None:
                    selected = mask[y:y+h, x:x+w]
                    require(bool(selected.any()), "text mask has no pixels in scene ROI")
                    row["text_psnr"] = psnr(float(np.mean(delta[selected] ** 2)))
                background = resources[packet["scene"]].get("backgroundRoi")
                row["background_psnr"] = None
                if background:
                    bx, by, bw, bh = background
                    difference = image[by:by+bh, bx:bx+bw].astype(np.float64) - reference[by:by+bh, bx:bx+bw]
                    row["background_psnr"] = psnr(float(np.mean(difference * difference)))
                row["output_us"] = int(packet["output_us"])
                metrics.append(row)
            if decoder is not None:
                require(not decoder.stdout.read(1), "decoder emitted extra frames")
                require(decoder.wait() == 0, "software decoder failed")
        finally:
            if decoder is not None and decoder.poll() is None:
                decoder.terminate()
                decoder.wait()
    with artifact(prefix, ".quality.csv").open("w") as file:
        writer = csv.DictWriter(file, fieldnames=list(metrics[0]) if metrics else packet_columns + ["roi_psnr", "text_psnr", "background_psnr"])
        writer.writeheader(); writer.writerows(metrics)
    if comparisons:
        from PIL import Image, ImageDraw, ImageFont
        try:
            font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 15)
        except OSError:
            font = ImageFont.load_default()
        for event_index, selected in comparisons.items():
            event = scenario["events"][event_index]
            x, y, w, h = resources[event["scene"]]["roi"]
            panels = [("Reference Y plane", Image.fromarray(np.asarray(references[event["scene"]])).crop((x, y, x+w, y+h)))]
            metadata = []
            for phase, row in selected:
                image = Image.fromarray(snapshots[int(row["wire_index"])])
                image.save(prefix.parent / f"{prefix.name}.event-{event_index}-{phase}.png")
                output_us = int(row["output_us"])
                offset_ms = (output_us - event["atMs"] * 1000) / 1000
                panels.append((f"{phase}: ready at +{offset_ms:.1f} ms", image.crop((x, y, x+w, y+h))))
                metadata.append({"phase": phase, "wireIndex": int(row["wire_index"]), "packetReadyOffsetMs": offset_ms,
                    "sourceEvent": int(row["event"]), "sourceScene": row["scene"]})
            canvas = Image.new("RGB", (len(panels) * (w + 12) - 12, h + 42), "#ededed")
            draw = ImageDraw.Draw(canvas)
            for index, (label, image) in enumerate(panels):
                draw.text((index * (w+12) + 4, 8), label, fill="black", font=font)
                canvas.paste(image, (index * (w+12), 42))
            canvas.save(prefix.parent / f"{prefix.name}.event-{event_index}-comparison.png")
            (prefix.parent / f"{prefix.name}.event-{event_index}-comparison.json").write_text(json.dumps({
                "event": event_index, "scene": event["scene"], "roi": [x,y,w,h],
                "note": "Exact Y-plane comparison. display-at-3s selects the packet still displayed then, not a fictitious new capture.",
                "missingOutput": not selected,
                "panels": metadata}, indent=2) + "\n")
    text_threshold = scenario.get("quality", {}).get("textPsnrDb")
    threshold = text_threshold if text_threshold is not None else scenario.get("quality", {}).get("psnrDb", 35.0)
    metric = "text_psnr" if text_threshold is not None else "roi_psnr"
    deadline = scenario.get("quality", {}).get("maxRecoveryMs", 750)
    results = []
    for index, event in enumerate(scenario["events"]):
        start = event["atMs"] * 1000
        end = (scenario["events"][index + 1]["atMs"] if index + 1 < len(scenario["events"]) else scenario["durationMs"]) * 1000
        samples = [row for row in metrics if int(row["event"]) == index and start <= row["output_us"] < end]
        require(not samples or all(row[metric] is not None for row in samples), "selected quality metric is missing")
        first_good = next((row for row in samples if row[metric] >= threshold), None)
        first_good_ms = None if first_good is None else (first_good["output_us"] - start) / 1000
        # Recovery starts at the earliest frame whose entire remaining output
        # suffix stays clear. A brief early good frame cannot conceal a later
        # regression; a final stable recovery before the deadline may still pass.
        recovery_index = None
        for sample_index in range(len(samples) - 1, -1, -1):
            if samples[sample_index][metric] < threshold:
                break
            recovery_index = sample_index
        recovered = [] if recovery_index is None else samples[recovery_index:]
        recovery = None if not recovered else (recovered[0]["output_us"] - start) / 1000
        minimum_quality = None if not recovered else min(row[metric] for row in recovered)
        minimum_text = None if not recovered or any(row["text_psnr"] is None for row in recovered) else min(row["text_psnr"] for row in recovered)
        stable = bool(recovered) and minimum_quality >= threshold
        first_output = None if not samples else (samples[0]["output_us"] - start) / 1000
        delivery_pass = first_output is not None and first_output <= deadline
        verify = event.get("verify", index > 0 and end - start >= deadline * 1000)
        passed = not verify or (recovery is not None and recovery <= deadline and stable)
        results.append({"event": index, "scene": event["scene"], "atMs": event["atMs"], "verify": verify,
            "outputs": len(samples), "bytes": sum(int(row["bytes"]) for row in samples),
            "firstPsnr": None if not samples else samples[0]["roi_psnr"],
            "lastPsnr": None if not samples else samples[-1]["roi_psnr"],
            "firstTextPsnr": None if not samples else samples[0]["text_psnr"],
            "lastTextPsnr": None if not samples else samples[-1]["text_psnr"],
            "firstBackgroundPsnr": None if not samples else samples[0]["background_psnr"],
            "lastBackgroundPsnr": None if not samples else samples[-1]["background_psnr"],
            "firstOutputMs": first_output, "deliveryPass": delivery_pass,
            "firstGoodMs": first_good_ms, "qualityStableAfterRecovery": stable,
            "minQualityAfterRecovery": minimum_quality, "minTextAfterRecovery": minimum_text,
            "recoveryMs": recovery, "pass": passed})
    from saver_wire import estimate_run
    budget = scenario.get("quality", {}).get("maxProjectedMBPerHour", 300)
    wire = {}
    network, acceptance_minimum = resolve_network(scenario, packet_size)
    for minimum in sorted({1, 2, acceptance_minimum}):
        wire[f"minParity{minimum}"] = estimate_run([int(row["bytes"]) for row in packets],
            duration_seconds=scenario["durationMs"] / 1000, budget_mb_per_hour=budget,
            min_parity=minimum, **network)
    wire.update({"packetSize": network["packet_size"], "fecPercentage": network["fec_percentage"],
        "ipVersion": network["ip_version"], "encrypted": network["encrypted"],
        "acceptanceMinParity": acceptance_minimum, "encodedBytesIncludeShortHeader": False,
        "note": "IP video estimate, not measured carrier/VPN/audio/control usage. Clean saver uses parity minimum one; acceptance checks the configured minimum (at least two)."})
    require(any(row["verify"] for row in results), "timeline has no quality assertions")
    quality_pass = all(row["pass"] for row in results)
    verified = [row for row in results if row["verify"]]
    accepted = wire[f"minParity{acceptance_minimum}"]
    budget_pass = accepted["video_ip_budget_met"] and accepted["protocol_recoverable"]
    source_record = prefix.parent / "replay-source.json"
    scope = "production asynchronous replay; see extracted-source coverage and adapter record" if source_record.exists() else "native codec/common-policy benchmark"
    report = {"schemaVersion": 1, "scope": scope,
        "producerSourceRecord": str(source_record) if source_record.exists() else None,
        "metric": metric, "thresholdDb": threshold, "maxRecoveryMs": deadline,
        "verifiedEvents": len(verified), "deliveryPass": all(row["deliveryPass"] for row in verified),
        "qualityStableAfterRecovery": all(row["qualityStableAfterRecovery"] for row in verified),
        "minTextAfterRecovery": min((row["minTextAfterRecovery"] for row in verified if row["minTextAfterRecovery"] is not None), default=None),
        "missingOutputEvents": sum(row["outputs"] == 0 for row in results),
        "missingVerifiedOutputEvents": sum(row["outputs"] == 0 for row in verified),
        "lateVerifiedOutputEvents": sum(row["firstOutputMs"] is not None and row["firstOutputMs"] > deadline for row in verified),
        "qualityPass": quality_pass, "videoIpBudgetPass": budget_pass, "wireEstimate": wire,
        "pass": quality_pass and budget_pass, "events": results}
    report["encoderRuntime"] = runtime
    report["analysisProvenance"] = {
        "analyzerSha256": hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
        "decoder": ffmpeg,
        "decoderVersion": subprocess.run([ffmpeg, "-version"], capture_output=True, text=True, check=True).stdout.splitlines()[0],
        "resources": {key: {name: value[name] for name in ("sourceSha256", "nv12Sha256", "textMaskSha256") if name in value}
                      for key, value in resources.items()},
    }
    artifact(prefix, ".report.json").write_text(json.dumps(report, indent=2) + "\n")
    for event in results:
        if event["verify"]:
            print(f"{'PASS' if event['pass'] else 'FAIL'} event={event['event']} scene={event['scene']} recovery_ms={event['recoveryMs']} first_good_ms={event['firstGoodMs']} stable={event['qualityStableAfterRecovery']} min_text_after_recovery={event['minTextAfterRecovery']} delivery_ms={event['firstOutputMs']} delivery_pass={event['deliveryPass']} first_roi={event['firstPsnr']} last_roi={event['lastPsnr']} first_text={event['firstTextPsnr']} last_text={event['lastTextPsnr']}")
    print(f"{'PASS' if budget_pass else 'FAIL'} video_ip_budget min1_mb_h={wire['minParity1']['estimated_video_ip_mb_per_hour']:.3f} min2_mb_h={wire['minParity2']['estimated_video_ip_mb_per_hour']:.3f} limit_mb_h={budget}")
    return report["pass"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeline", "--scenario", dest="timeline", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--ffmpeg-root", type=pathlib.Path)
    parser.add_argument("--decoder", default="ffmpeg")
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--analyze-prefix", type=pathlib.Path)
    parser.add_argument("--pts-mode", choices=("sequential", "elapsed"), default="sequential",
                        help="Native benchmark only: historical sequential PTS or elapsed-time diagnostic")
    parser.add_argument("--saver", choices=("on", "off"), default="on")
    parser.add_argument("--compare-event", type=int, action="append", default=[], help="Export reference/first/held-at-3s synthetic Y-plane PNG comparison")
    parser.add_argument("--packet-size", type=int, help="Sunshine/SDP packetSize override; must agree with explicit timeline network configuration")
    args = parser.parse_args()
    require(sys.platform == "darwin" or args.prepare_only or args.analyze_prefix, "native encoder requires macOS")
    output = args.output.resolve()
    require(not output.is_relative_to(ROOT), "test output must be outside the source checkout")
    require(not re.search(r"\s", str(output)), "test output path cannot contain whitespace")
    output.mkdir(parents=True, exist_ok=True)
    ffmpeg = shutil.which(args.decoder)
    require(ffmpeg, "software FFmpeg decoder/converter is required")
    scenario = load_scenario(args.timeline.resolve())
    resources, scenes_file, events_file = prepare(scenario, output, ffmpeg)
    if args.prepare_only:
        print(json.dumps({"scenes": str(scenes_file), "events": str(events_file), "resources": str(output / 'resources.json')}))
        return 0
    prefix = args.analyze_prefix.resolve() if args.analyze_prefix else output / f"replay-{args.pts_mode}-{args.saver}"
    if not args.analyze_prefix:
        require(not artifact(prefix, ".hevc").exists(), "encoded output already exists; choose a new output directory")
        require(args.ffmpeg_root, "--ffmpeg-root is required to compile the native runner")
        binary = compile_runner(args.ffmpeg_root.resolve(), output)
        command = [str(binary), str(scenario["width"]), str(scenario["height"]), str(scenario.get("nominalFps", 60)),
            str(scenario.get("bitrateKbps", 992)), str(scenario.get("burst", 3)), str(scenario["durationMs"]),
            args.pts_mode, "1", str(scenes_file), str(events_file), str(prefix), str(int(args.saver == "on"))]
        with artifact(prefix, ".encode.log").open("w") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        artifact(prefix, ".command.json").write_text(json.dumps({"argv": command,
            "sourceSha256": hashlib.sha256((ROOT / 'tests/vt-data-saver.cpp').read_bytes()).hexdigest(),
            "timelineSha256": hashlib.sha256(args.timeline.read_bytes()).hexdigest()}, indent=2) + "\n")
    return 0 if analyze(scenario, resources, prefix, ffmpeg, args.compare_event, args.packet_size) else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(2)
