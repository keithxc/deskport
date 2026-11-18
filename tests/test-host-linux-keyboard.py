#!/usr/bin/env python3
"""Check both Linux keyboard identities without creating devices or sending input."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import hashlib
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tarfile
import tempfile


ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / 'host/overlays/patch-host-linux-keyboard.py'
NIX_INPUT = 'src/platform/linux/input/inputtino_common.h'
PORTABLE_INPUT = 'src/platform/virtualhid_input.cpp'
PROFILES = 'third-party/libvirtualhid/src/core/profiles.cpp'


def extract(manifest_name, destination, names):
    vendor = ROOT / 'host/vendor'
    manifest = json.loads((vendor / manifest_name).read_text())
    archive = vendor / manifest['archive']
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == manifest['sha256']
    with tarfile.open(archive) as source:
        for name in names:
            matches = [m for m in source.getmembers()
                       if m.name == name or m.name.endswith('/' + name)]
            assert len(matches) == 1 and matches[0].isfile(), name
            path = destination / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(source.extractfile(matches[0]).read())


def section(source, start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


def patch_and_check(work, relative, anchor):
    path = work / relative
    original = path.read_text()
    subprocess.run([sys.executable, str(PATCH), str(work)], check=True)
    patched = path.read_text()
    assert patched != original
    subprocess.run([sys.executable, str(PATCH), str(work)], check=True)
    assert path.read_text() == patched, 'second application changed the source'
    # Upstream anchor removal or duplication must fail without a partial edit.
    for drift in (original.replace(anchor, '// upstream keyboard setup changed'),
                  original + '\n' + anchor + '\n'):
        path.write_text(drift)
        result = subprocess.run([sys.executable, str(PATCH), str(work)],
                                capture_output=True, text=True)
        assert result.returncode != 0 and 'anchor mismatch' in result.stderr
        assert path.read_text() == drift, 'failed patch changed the source'
    path.write_text(patched)
    return original, patched


def compile_and_run(work, label, source, flags=(), extra_sources=(), expected=0):
    unit, binary = work / (label + '.cpp'), work / label
    unit.write_text(source)
    command = shlex.split(os.environ.get('CXX', 'c++'))
    subprocess.run(command + ['-std=c++20', '-Wall', '-Wextra', '-Werror',
                             *flags, str(unit), *map(str, extra_sources),
                             '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)])
    assert result.returncode == expected, (label, result.returncode, expected)


def test_nix(work):
    extract('sunshine-nix.json', work, [NIX_INPUT,
            'third-party/inputtino/include/inputtino/input.hpp',
            'third-party/inputtino/include/inputtino/result.hpp',
            'src/platform/linux/input/inputtino_keyboard.cpp'])
    event_file = work / 'src/platform/linux/input/inputtino_keyboard.cpp'
    events_before = event_file.read_bytes()
    original, patched = patch_and_check(
        work, NIX_INPUT, '.name = inputtino_name_for_seat("Keyboard passthrough"sv),')
    assert event_file.read_bytes() == events_before
    initializer = lambda source: section(source, '        keyboard(inputtino::Keyboard::create({',
                                         '        gamepads(MAX_GAMEPADS)')
    # All routing, key-up handling, mouse and seat logic remain byte-for-byte intact.
    assert original.replace(initializer(original), '') == patched.replace(initializer(patched), '')
    helper = section(patched, '  inline std::string inputtino_name_for_seat(', '  using joypads_t')
    preamble = '''
#include <inputtino/input.hpp>
#include <string_view>
using namespace std::literals;
namespace inputtino_seat {
std::string target;
std::string get_target_seat() { return target; }
}
'''
    for label, source, expected in [('original', original, 41), ('patched', patched, 0)]:
        # Compile the actual initializer with inputtino's actual device type.
        fields = section(initializer(source), '{', '        })),') + '}'
        harness = preamble + helper + '\ninputtino::DeviceDefinition keyboard() { return ' + fields + '; }\n'
        harness += '''
int main() {
  for (const auto seat : {"", "seat0", "seat-test"}) {
    inputtino_seat::target = seat;
    const auto device = keyboard();
    const auto suffix = inputtino_seat::target == "seat-test" ? " (seat-test)" : "";
    if (device.name != std::string("DeskPort Keyboard") + suffix) return 41;
    if (device.name.find("Keyboard passthrough") != std::string::npos) return 42;
    if (device.vendor_id != 0xBEEF || device.product_id != 0xDEAD || device.version != 0x111) return 43;
  }
}
'''
        compile_and_run(work, 'nix-' + label, harness,
                        ['-I' + str(work / 'third-party/inputtino/include')], expected=expected)


def test_portable(work):
    extract('sunshine.json', work, [PORTABLE_INPUT, PROFILES,
            'third-party/libvirtualhid/src/include/libvirtualhid/profiles.hpp',
            'third-party/libvirtualhid/src/include/libvirtualhid/types.hpp'])
    original, patched = patch_and_check(work, PORTABLE_INPUT,
                                       '    options.profile = lvh::profiles::keyboard();')
    def options(source):
        method = section(source, '  void input_context_t::refresh_keyboard()',
                         '  void input_context_t::refresh_mouse()')
        return section(method, '    lvh::CreateKeyboardOptions options;', '    auto created =')
    # Event delivery, device lifetime, repeat, failure handling and other devices
    # are outside the changed options block and must remain identical.
    assert original.replace(options(original), '') == patched.replace(options(patched), '')
    preamble = '''
#include <libvirtualhid/profiles.hpp>
#include <string>
'''
    # Set the macro after system headers so a Mac can check the Linux branch
    # without pretending its system standard library is a Linux installation.
    for label, source, linux, expected in [('original-linux', original, True, 41),
                                          ('patched-linux', patched, True, 0),
                                          ('patched-nonlinux', patched, False, 0)]:
        platform = '\n#undef __linux__\n' + ('#define __linux__ 1\n' if linux else '')
        harness = preamble + platform + '\nlvh::CreateKeyboardOptions keyboard() {\n'
        harness += options(source) + 'return options;\n}\n'
        harness += '''
int main() {
  const auto options = keyboard();
  const auto baseline = lvh::profiles::keyboard();
  const auto &profile = options.profile;
#ifdef __linux__
  if (profile.name != "DeskPort Keyboard") return 41;
  if (profile.name.find("Keyboard passthrough") != std::string::npos) return 42;
#else
  if (profile.name != baseline.name) return 41;
#endif
  if (profile.device_type != baseline.device_type || profile.bus_type != baseline.bus_type ||
      profile.vendor_id != baseline.vendor_id || profile.product_id != baseline.product_id ||
      profile.version != baseline.version || profile.report_descriptor != baseline.report_descriptor ||
      profile.input_report_size != baseline.input_report_size ||
      profile.output_report_size != baseline.output_report_size ||
      profile.manufacturer != baseline.manufacturer) return 43;
  if (options.stable_id != "sunshine-keyboard" || options.auto_repeat_interval_ms != 50) return 44;
}
'''
        compile_and_run(work, 'portable-' + label, harness,
                        ['-I' + str(work / 'third-party/libvirtualhid/src/include')],
                        [work / PROFILES], expected)


def main():
    output = os.environ.get('DESKPORT_TEST_DIR') or os.environ.get('TMPDIR')
    if not output:
        checkout = subprocess.check_output(
            ['bash', str(ROOT / 'scripts/build-paths.sh'), 'checkout'], text=True).strip()
        run = 'linux-keyboard-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') + f'-{os.getpid()}'
        output = str(Path(checkout) / 'runs.noindex' / run / 'linux-nix/tests')
    Path(output).mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='linux-keyboard-', dir=output) as temporary:
        work = Path(temporary)
        test_nix(work / 'nix')
        test_portable(work / 'portable')
        missing = subprocess.run([sys.executable, str(PATCH), str(work / 'unsupported')],
                                 capture_output=True, text=True)
        assert missing.returncode != 0 and 'Unsupported' in missing.stderr
    print('PASS: both vendor keyboard identities, seat suffixes, non-Linux profile, '
          'unchanged input paths, idempotency, drift rejection and original-source negative controls')


if __name__ == '__main__':
    main()
