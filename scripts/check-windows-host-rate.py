#!/usr/bin/env python3
"""Compile changed Windows host units using an existing pinned compile database.

Runs inside winbuild/scripts/shell.nix on the x86_64 build host under the
caller's shared heavy-build lock. Reads
cached dependency inputs without modifying that build or creating an installer.
"""
import argparse
import json
from pathlib import Path
import shlex
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compile-commands', type=Path, required=True)
parser.add_argument('--prepare-only', action='store_true', help='Check the full source overlay chain without compiling')
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
build = Path(subprocess.check_output(['bash', str(repo / 'scripts/build-paths.sh'), 'checkout'], text=True).strip()) / 'live-bitrate'
source = build / 'windows-source'
evidence = build / 'evidence'
evidence.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(repo / 'scripts/prepare-host-source.py'), str(source)], check=True)
# Windows display/rate overlays expect the same reviewed cross-build patch as
# winbuild/scripts/prepare-host.sh; do not apply them to raw upstream sources.
subprocess.run(['git', '-C', str(source), 'apply', '--recount',
                str(repo / 'host/windows/patches/sunshine-cross-build.patch')], check=True)
for overlay in ('windows-display', 'windows-desktop-lifetime', 'windows-pointer-lifetime',
                'diagnostics', 'network', 'smart-stream', 'session-settings', 'session-takeover',
                'encoder-policy', 'input-activity', 'sync-cadence', 'remote-rate', 'data-saver',
                'video-pause', 'rate-api'):
    subprocess.run(['python3', str(repo / f'host/overlays/patch-host-{overlay}.py'), str(source)], check=True)
subprocess.run(['python3', str(repo / 'host/overlays/patch-host-browser.py'), str(source),
                str(repo / 'host/browser')], check=True)
headers = source / 'src/deskport/common'
headers.mkdir(parents=True, exist_ok=True)
for name in ('smartstream', 'inputactivity', 'framecadence', 'encoderpolicy', 'ratecontrol'):
    shutil.copyfile(repo / f'host/common/{name}.h', headers / f'{name}.h')
if args.prepare_only:
    print('PASS: Windows source overlay preparation (no compilation)')
    raise SystemExit(0)
commands = json.loads(args.compile_commands.read_text())
count = 0
for entry in commands:
    old_file = Path(entry['file'])
    if old_file.name not in ('nvenc_base.cpp', 'video.cpp', 'confighttp.cpp', 'rtsp.cpp', 'stream.cpp'):
        continue
    old_root = next(p for p in old_file.parents if (p / 'src/video.cpp').exists())
    new_file = source / old_file.relative_to(old_root)
    raw = entry.get('arguments') or shlex.split(entry['command'])
    command, skip = [], False
    # Strip writes into the existing build directory; all new .obj/logs belong
    # to this checkout's build root. Native dependencies are read-only inputs.
    for arg in raw:
        if skip:
            skip = False
            continue
        if arg in ('-o', '-MF', '-MT', '-MQ'):
            skip = True
            continue
        if arg in ('-MD', '-MMD', '-MP', '-c') or arg == str(old_file):
            continue
        command.append(arg)
    sdk = next((arg.partition('=')[2] for arg in command if arg.startswith('-DNVENC_SDK_VERSION=')), 'host')
    label = f'{old_file.stem}-{sdk}'
    command += ['-I', str(source), '-O0', '-c', str(new_file), '-o', str(build / f'{label}.obj')]
    # Prefer the prepared production headers to the cached source headers.
    command[1:1] = ['-I', str(source)]
    (evidence / f'windows-{label}-command.json').write_text(json.dumps(command, indent=2) + '\n')
    with (evidence / f'windows-{label}.log').open('w') as output:
        subprocess.run(command, cwd=entry['directory'], stdout=output, stderr=subprocess.STDOUT, check=True)
    print(f'PASS: Windows target compiler: {label}', flush=True)
    count += 1
assert count >= 7, f'Missing expected 3 SDK variants + host translation units: {count}'
