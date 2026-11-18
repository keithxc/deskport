#!/usr/bin/env python3
"""Exercise bundled Qt Wayland across isolated KWin virtual-display sessions.

Build the probe separately with the AppImage builder's Qt headers:
  python3 tests/test-appimage-display-session.py --build-probe /work/qt-screen-probe
Run inside a distribution container/VM, with TMPDIR under the test output root:
  python3 tests/test-appimage-display-session.py /candidate --probe /work/qt-screen-probe \
      --output-probe /work/output-probe --results /work/candidate-test

The compositor and output probe use the distribution runtime. The persistent Qt
probe uses the selected AppImage Qt/Wayland libraries, verified by content hashes
of /proc mappings. Override --fixture-loader and --fixture-library-path when
testing an AppRun-selected host-compatible graphics cohort. The actual GUI is
also launched through AppRun, with host autostart and the web server disabled.
This test exercises output topology, GUI survival and permission identity; it
does not claim a network stream or remote-input acceptance.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time


def arguments():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('appdir', nargs='?', type=Path)
    parser.add_argument('--build-probe', type=Path, help='Only compile the Qt GUI fixture to this path')
    parser.add_argument('--qmake', help='qmake from the package builder (Qt 6.4.2 for rc.4)')
    parser.add_argument('--probe', type=Path, help='Previously compiled tests/appimage-qt-screen-probe.cpp')
    parser.add_argument('--output-probe', type=Path, help='Previously compiled tests/linux-output-probe.cpp')
    parser.add_argument('--results', type=Path, required=False, help='Retained JSON evidence and owned-process logs')
    parser.add_argument('--fixture-loader', default='apprun', help='apprun (same graphics cohort as GUI), private, host, or an explicit ELF loader path')
    parser.add_argument('--fixture-library-path', help='Explicit loader library path for an AppRun graphics cohort')
    parser.add_argument('--seed-permission', action='store_true', help='Seed isolated fixture grants for a broken old baseline; skip automatic permission acceptance')
    parser.add_argument('--cycles', type=int, default=3)
    parser.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.build_probe:
        return args
    if not args.appdir or not args.probe or not args.output_probe or not args.results:
        parser.error('appdir, --probe, --output-probe and --results are required for a runtime test')
    if args.cycles < 1:
        parser.error('--cycles must be at least one')
    return args


def build_probe(args):
    destination = args.build_probe.resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    qmake = args.qmake or shutil.which('qmake6') or shutil.which('qmake')
    if not qmake:
        raise RuntimeError('A Qt 6 qmake plus Qt Core/Gui and wayland-client headers are required')
    source = Path(__file__).resolve().parents[1] / 'tests/appimage-qt-screen-probe.cpp'
    with tempfile.TemporaryDirectory(prefix='deskport-qt-probe-build-') as tmp:
        project = Path(tmp) / 'probe.pro'
        project.write_text('QT = core gui\nCONFIG += console c++17 link_pkgconfig\n'
                           'PKGCONFIG += wayland-client\nDEFINES += QT_NO_VERSION_TAGGING\n'
                           f'TARGET = qt-screen-probe\nSOURCES += "{source}"\n')
        subprocess.run([qmake, str(project)], cwd=tmp, check=True)
        subprocess.run(['make', '-j2'], cwd=tmp, check=True)
        shutil.copy2(Path(tmp) / 'qt-screen-probe', destination)
    print(f'BUILD probe={destination}', flush=True)


def permission_entry(executable):
    command = str(executable).replace('"', '"""')
    command = ('"' + command + '"').replace('\\', '\\\\').replace('\n', '\\n').replace('\r', '\\r')
    encoded = base64.b64encode(str(executable).encode()).decode()
    return ('[Desktop Entry]\nType=Application\nName=DeskPort display permission\nNoDisplay=true\n'
            f'Exec={command}\nX-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1\n'
            f'X-DeskPort-Executable={encoded}\n')


def outer(args):
    results = args.results.resolve()
    results.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='deskport-appimage-session-') as tmp:
        root = Path(tmp)
        runtime = root / 'runtime'
        runtime.mkdir(mode=0o700)
        env = dict(os.environ, HOME=str(root / 'home'), XDG_RUNTIME_DIR=str(runtime),
                   XDG_CONFIG_HOME=str(root / 'config'), XDG_DATA_HOME=str(root / 'data'),
                   XDG_CACHE_HOME=str(root / 'cache'), XDG_STATE_HOME=str(root / 'state'),
                   XDG_DATA_DIRS=str(root / 'data') + ':/usr/local/share:/usr/share',
                   WAYLAND_DISPLAY='deskport-appimage-test', XDG_CURRENT_DESKTOP='KDE',
                   QT_QPA_PLATFORM='wayland', QT_QUICK_BACKEND='software',
                   QT_FORCE_STDERR_LOGGING='1', QT_LOGGING_TO_CONSOLE='1', LIBGL_ALWAYS_SOFTWARE='1',
                   KWIN_COMPOSE='O2', DESKPORT_APPIMAGE_TEST_ROOT=tmp)
        for name in ('DISPLAY', 'DBUS_SESSION_BUS_ADDRESS', 'PIPEWIRE_REMOTE', 'LD_LIBRARY_PATH',
                     'LD_PRELOAD', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML2_IMPORT_PATH',
                     'QML_IMPORT_PATH', 'APPDIR', 'APPIMAGE', 'SHARUN_DIR', 'SHARUN_LDNAME',
                     'SHARUN_EXTRA_LIBRARY_PATH', 'DESKPORT_APPIMAGE_GRAPHICS_RUNTIME',
                     'KWIN_WAYLAND_NO_PERMISSION_CHECKS', 'XDG_MENU_PREFIX'):
            env.pop(name, None)
        Path(env['HOME']).mkdir()
        applications = root / 'data/applications'
        applications.mkdir(parents=True)
        menus = root / 'config/menus'
        menus.mkdir(parents=True)
        (menus / 'applications.menu').write_text('<!DOCTYPE Menu PUBLIC "-//freedesktop//DTD Menu 1.0//EN" "http://www.freedesktop.org/standards/menu-spec/1.0/menu.dtd"><Menu><Name>Applications</Name><DefaultAppDirs/><Include><All/></Include></Menu>')
        bus = root / 'session-bus.conf'
        bus.write_text('<busconfig><type>session</type><listen>unix:tmpdir=' + str(runtime) + '</listen><auth>EXTERNAL</auth><policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>')
        command = ['dbus-run-session', '--config-file=' + str(bus), '--', sys.executable,
                   str(Path(__file__).resolve()), *sys.argv[1:], '--inside']
        child = subprocess.Popen(command, env=env, start_new_session=True)
        try:
            status = child.wait(timeout=240 + args.cycles * 80)
        except BaseException:
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=20)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()
            raise
        if status:
            raise SystemExit(status)


def inside(args):
    appdir = args.appdir.resolve()
    results = args.results.resolve()
    root = Path(os.environ['DESKPORT_APPIMAGE_TEST_ROOT'])
    helper = appdir / 'usr/bin/deskport-display'
    if not helper.exists():
        helper = appdir / 'usr/libexec/deskport-display'
    if not helper.exists():
        raise RuntimeError('Missing AppImage display helper')
    for tool in ('pipewire', 'wireplumber', 'kwin_wayland', 'kbuildsycoca6'):
        if not shutil.which(tool):
            raise RuntimeError(f'Missing distribution test dependency: {tool}')
    loader_candidates = list((appdir / 'usr/shared/lib').glob('ld-linux-*.so*'))
    if args.fixture_loader in ('private', 'apprun'):
        if not loader_candidates and args.fixture_loader == 'private':
            raise RuntimeError('Private loader missing; select --fixture-loader host and the appropriate cohort path')
        loader = loader_candidates[0] if loader_candidates else Path('/lib64/ld-linux-x86-64.so.2')
    elif args.fixture_loader == 'host':
        loader = next((path for path in (Path('/lib64/ld-linux-x86-64.so.2'), Path('/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2')) if path.exists()), None)
        if loader is None:
            raise RuntimeError('No host x86_64 ELF loader found')
    else:
        loader = Path(args.fixture_loader).resolve()
    library_path = args.fixture_library_path or os.pathsep.join(str(p) for p in
        (appdir / 'usr/shared/lib', appdir / 'usr/lib') if p.is_dir())
    platforms = next(iter(appdir.glob('usr/**/platforms/libqwayland-generic.so')), None)
    if platforms is None:
        platforms = next(iter(appdir.glob('usr/**/platforms/libqwayland-egl.so')), None)
    if platforms is None:
        raise RuntimeError('Missing bundled Wayland platform plugin')
    fixture_env = dict(os.environ, QT_QPA_PLATFORM_PLUGIN_PATH=str(platforms.parent),
                       QT_PLUGIN_PATH=str(platforms.parent.parent), QML2_IMPORT_PATH=str(appdir / 'usr/qml'),
                       SPA_PLUGIN_DIR=str(appdir / 'usr/shared/lib/spa-0.2'),
                       PIPEWIRE_MODULE_DIR=str(appdir / 'usr/shared/lib/pipewire-0.3'),
                       PIPEWIRE_CONFIG_DIR=str(appdir / 'usr/share/pipewire'),
                       __EGL_VENDOR_LIBRARY_DIRS='/run/opengl-driver/share/glvnd/egl_vendor.d:/usr/share/glvnd/egl_vendor.d:' + str(appdir / 'usr/share/glvnd/egl_vendor.d'),
                       LIBGL_DRIVERS_PATH='/run/opengl-driver/lib/dri:/usr/lib/x86_64-linux-gnu/dri:/usr/lib64/dri:/usr/lib/dri:' + str(appdir / 'usr/shared/lib/mesa-software'),
                       LIBVA_DRIVERS_PATH='/run/opengl-driver/lib/dri:/usr/lib/x86_64-linux-gnu/dri:/usr/lib64/dri:/usr/lib/dri')
    command = [str(loader), '--library-path', library_path, str(args.probe.resolve())]
    if args.fixture_loader == 'apprun':
        if args.fixture_library_path:
            raise RuntimeError('--fixture-library-path requires an explicit/private/host loader')
        # Preserve the exact bundle's graphics policy. The shell only receives
        # paths as arguments; no generated or untrusted code is evaluated.
        script = '''set -eu
prefix=$1
if [ -f "$prefix/libexec/deskport-graphics-env.sh" ]; then
    . "$prefix/libexec/deskport-graphics-env.sh"
    deskport_graphics_env "$prefix" deskport
fi
loader=${SHARUN_LDNAME:-$3}
cohort=${DESKPORT_APPIMAGE_GRAPHICS_RUNTIME:-${SHARUN_EXTRA_LIBRARY_PATH:-}}
exec "$loader" --library-path "${cohort:+$cohort:}$prefix/shared/lib:$prefix/lib" "$2"
'''
        command = ['/bin/sh', '-c', script, 'deskport-qt-cohort', str(appdir / 'usr'), str(args.probe.resolve()), str(loader)]
    children = []
    logs = []
    evidence = {'appdir': str(appdir), 'fixtureCommand': command, 'fixturePluginPath': str(platforms.parent),
                'permissionMode': 'seeded-fixture' if args.seed_permission else 'automatic', 'observations': [], 'passed': False}
    def launch(label, argv, env=None, protocol=False):
        log = (results / f'{label}.log').open('w+')
        logs.append(log)
        child = subprocess.Popen(argv, env=env, stdin=subprocess.PIPE if protocol else subprocess.DEVNULL,
                                 stdout=subprocess.PIPE if protocol else log, stderr=log,
                                 text=True, start_new_session=True)
        children.append(child)
        evidence.setdefault('processes', []).append({'label': label, 'pid': child.pid, 'command': argv})
        print(f'RESOURCE pid={child.pid} label={label} command={argv!r}', flush=True)
        return child
    def stop(child):
        # Also terminate inherited descendants if the leader already crashed.
        try:
            os.killpg(child.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        if child.poll() is None:
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()
        print(f'RESOURCE stopped pid={child.pid} status={child.returncode}', flush=True)
    def wait_path(path, child, timeout=15):
        deadline = time.monotonic() + timeout
        while not path.exists() and child.poll() is None and time.monotonic() < deadline:
            time.sleep(.05)
        assert path.exists(), f'Timed out waiting for {path}; child status={child.poll()}'
    def receive(child, timeout=12):
        assert select.select([child.stdout], [], [], timeout)[0], f'Protocol timeout pid={child.pid} status={child.poll()}'
        line = child.stdout.readline()
        assert line, f'Protocol EOF pid={child.pid} status={child.poll()}'
        value = json.loads(line)
        assert 'error' not in value, value
        return value
    def send(child, payload):
        assert child.poll() is None, f'Child exited: pid={child.pid} status={child.poll()}'
        child.stdin.write(payload + '\n')
        child.stdin.flush()
        return receive(child)
    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()
    def verify_libc(child_pid, label):
        maps = Path(f'/proc/{child_pid}/maps').read_text().splitlines()
        runtime = sorted({Path(line.split()[-1]) for line in maps
                          if '/libc.so.6' in line or '/ld-linux-' in line})
        checked = [{'path': str(path), 'sha256': digest(path)} for path in runtime]
        evidence.setdefault('runtimeMappings', []).append({'label': label, 'pid': child_pid, 'libraries': checked})
        libc = {digest(path) for path in runtime if path.name == 'libc.so.6'}
        loaders = {digest(path) for path in runtime if path.name.startswith('ld-linux-')}
        assert libc and loaders, f'{label} did not expose libc and loader file mappings'
        return {'libc': libc, 'loader': loaders}
    def verify_qt(child, label):
        maps = Path(f'/proc/{child.pid}/maps').read_text().splitlines()
        mapped = [Path(line.split()[-1]) for line in maps
                  if '/libQt6' in line or '/libqwayland-' in line]
        mapped = sorted(set(mapped))
        names = ('libQt6Core.so', 'libQt6Gui.so', 'libQt6WaylandClient.so')
        assert all(any(path.name.startswith(name) for path in mapped) for name in names), (label, mapped)
        assert any(path.name.startswith('libqwayland-') for path in mapped), (label, mapped)
        checked = []
        for path in mapped:
            expected = [p for p in appdir.glob('usr/**/' + path.name) if p.is_file()]
            assert expected and any(digest(p) == digest(path) for p in expected), f'{label} loaded Qt outside the selected bundle: {path}'
            checked.append({'path': str(path), 'sha256': digest(path)})
        backport = appdir / 'usr/share/doc/deskport/qtwayland-backport/source.json'
        if backport.is_file():
            metadata = json.loads(backport.read_text())
            assert metadata.get('modified') is True, metadata
            files = metadata['runtime_files']
            assert len(files) == 1, files
            expected_library = appdir / files[0]['path']
            expected_hash = files[0]['checksum'].removeprefix('SHA256:')
            assert digest(expected_library) == expected_hash, 'Modified Qt library does not match its provenance'
            wayland = [item for item in checked if Path(item['path']).name.startswith('libQt6WaylandClient.so')]
            assert len(wayland) == 1 and wayland[0]['sha256'] == expected_hash, f'{label} did not load the verified Qt Wayland backport: {wayland}'
            evidence[label + 'QtWaylandBackport'] = dict(upstreamFix=metadata['upstream_fix'],
                library=wayland[0], modified=True)
        evidence[label + 'QtMappings'] = checked
        return verify_libc(child.pid, label)
    def outputs(devices=False):
        command = [str(args.output_probe.resolve())] + (['--devices'] if devices else [])
        return {item['name']: {k: v for k, v in item.items() if k != 'id'}
                for item in json.loads(subprocess.check_output(command, timeout=5))}
    def snapshot(label, qt, gui):
        time.sleep(.2)
        assert gui.poll() is None, f'AppRun GUI exited during {label}: {gui.poll()}'
        state = send(qt, 'snapshot')
        assert state['platform'].startswith('wayland'), state
        assert state['activeWlOutputs'] > 0 and state['minActiveWlOutputs'] > 0, state
        assert state['screens'] and state['offscreenValid'], state
        evidence['observations'].append({'phase': label, 'qt': state, 'outputs': outputs(), 'devices': outputs(True)})
        print(f'OBSERVE {label} screens={len(state["screens"])} wl_output={state["activeWlOutputs"]} minimum={state["minActiveWlOutputs"]} frames={state["frames"]}', flush=True)
        return state
    try:
        if args.seed_permission:
            applications = Path(os.environ['XDG_DATA_HOME']) / 'applications'
            for index, executable in enumerate([helper.resolve(), appdir / 'usr/shared/bin/deskport-display', *loader_candidates]):
                if executable.exists():
                    (applications / f'io.github.keithxc.DeskPort.fixture-{index}.desktop').write_text(permission_entry(executable.resolve()))
        subprocess.run(['kbuildsycoca6', '--noincremental'], check=True, stdout=subprocess.DEVNULL)
        pipewire = launch('pipewire', ['pipewire'])
        wait_path(Path(os.environ['XDG_RUNTIME_DIR']) / 'pipewire-0', pipewire)
        wp_config = root / 'wireplumber'
        wp_config.mkdir()
        wp_source = next((p for p in (Path('/usr/share/wireplumber/wireplumber.conf'), Path(shutil.which('wireplumber')).resolve().parent.parent / 'share/wireplumber/wireplumber.conf') if p.exists()), None)
        assert wp_source is not None, 'wireplumber.conf missing'
        shutil.copy2(wp_source, wp_config / 'wireplumber.conf')
        (wp_config / 'wireplumber.conf.d').mkdir()
        (wp_config / 'wireplumber.conf.d/90-isolated.conf').write_text('wireplumber.profiles = { main = { monitor.alsa = disabled monitor.bluez = disabled monitor.bluez-midi = disabled monitor.v4l2 = disabled monitor.libcamera = disabled } }')
        launch('wireplumber', ['wireplumber'], env=dict(os.environ, WIREPLUMBER_CONFIG_DIR=str(wp_config)))
        kwin = launch('kwin', ['kwin_wayland', '--virtual', '--output-count', '3', '--width', '1280', '--height', '720',
                               '--socket', os.environ['WAYLAND_DISPLAY'], '--no-lockscreen', '--no-global-shortcuts', '--no-kactivities'],
                      env=dict(os.environ, QT_QPA_PLATFORM='offscreen'))
        wait_path(Path(os.environ['XDG_RUNTIME_DIR']) / os.environ['WAYLAND_DISPLAY'], kwin)
        baseline = outputs()
        baseline_devices = outputs(True)
        assert len(baseline) == 3, baseline
        evidence['baseline'] = {'outputs': baseline, 'devices': baseline_devices}
        qt = launch('qt-probe', command, env=fixture_env, protocol=True)
        initial = receive(qt)
        assert initial.get('ready'), initial
        gui = launch('appimage-gui', [str(appdir / 'AppRun'), '--no-host-autostart', '--no-web-server'])
        time.sleep(2)
        snapshot('baseline', qt, gui)
        fixture_libc = verify_qt(qt, 'fixture')
        gui_libc = verify_qt(gui, 'gui')
        if args.fixture_loader == 'apprun':
            assert fixture_libc and fixture_libc == gui_libc, 'Fixture and AppRun selected different glibc cohorts'
        # Match HostManager's child inheritance, including sharun's consumed
        # EXTRA variable. Re-sourcing graphics-env for this process would hide
        # the regression in helper/recovery children of the actual viewer.
        gui_environment = {}
        for field in Path(f'/proc/{gui.pid}/environ').read_bytes().split(b'\0'):
            if b'=' in field:
                key, value = field.split(b'=', 1)
                gui_environment[os.fsdecode(key)] = os.fsdecode(value)
        assert gui_environment.get('XDG_RUNTIME_DIR') == os.environ['XDG_RUNTIME_DIR'], 'GUI environment escaped the isolated runtime'
        assert gui_environment.get('XDG_DATA_HOME') == os.environ['XDG_DATA_HOME'], 'GUI environment escaped the isolated data directory'
        evidence['guiRuntimeEnvironment'] = {key: gui_environment.get(key) for key in
            ('SHARUN_LDNAME', 'SHARUN_EXTRA_LIBRARY_PATH', 'DESKPORT_APPIMAGE_GRAPHICS_RUNTIME', 'LD_LIBRARY_PATH', 'APPDIR', 'XDG_RUNTIME_DIR')}
        # /proc/environ can retain the initial bytes after sharun's in-process
        # unsetenv. Its pinned launcher consumes this one-shot value; children
        # inherit the persistent selection only, as the native sharun test proves.
        gui_environment.pop('SHARUN_EXTRA_LIBRARY_PATH', None)
        recovery_seen = set()
        def verify_recovery(display):
            child_list = Path(f'/proc/{display.pid}/task/{display.pid}/children').read_text().split()
            recovery = []
            for value in child_list:
                child_pid = int(value)
                try:
                    command_line = Path(f'/proc/{child_pid}/cmdline').read_bytes().split(b'\0')
                except FileNotFoundError:
                    continue
                if b'--restore-kwin' in command_line:
                    recovery.append(child_pid)
            assert recovery, 'Active helper has no independent KWin layout recovery process'
            for child_pid in recovery:
                if child_pid in recovery_seen:
                    continue
                actual = verify_libc(child_pid, 'helper-recovery')
                assert actual and actual == gui_libc, 'Recovery process mixed a different glibc with the viewer loader'
                evidence.setdefault('recoveryIdentities', []).append({'pid': child_pid, 'exe': str(Path(f'/proc/{child_pid}/exe').resolve())})
                recovery_seen.add(child_pid)
        for policy in (0, 1):
            display = launch(f'helper-policy-{policy}', [str(helper), '1280', '720'],
                             env=dict(gui_environment, DESKPORT_DISPLAY_ON_DEMAND='1'), protocol=True)
            ready = receive(display)
            assert ready.get('ready') and not ready.get('active'), ready
            helper_libc = verify_libc(display.pid, f'helper-policy-{policy}')
            assert helper_libc and helper_libc == gui_libc, 'Display helper mixed a different glibc with the viewer loader'
            identity = Path(f'/proc/{display.pid}/exe').resolve()
            evidence.setdefault('helperIdentities', []).append({'pid': display.pid, 'exe': str(identity), 'expected': str(helper.resolve())})
            if not args.seed_permission:
                own_executables = {path.resolve() for path in (helper, appdir / 'usr/shared/bin/deskport-display') if path.exists()}
                assert identity in own_executables and identity.is_relative_to(appdir), f'Helper /proc identity is outside the expected AppDir helper executables: {identity}'
                with identity.open('rb') as executable:
                    assert executable.read(4) == b'\x7fELF', f'Helper /proc identity is not an ELF executable: {identity}'
                entries = list((Path(os.environ['XDG_DATA_HOME']) / 'applications').glob('*.desktop'))
                matching = []
                for entry in entries:
                    values = dict(line.split('=', 1) for line in entry.read_text().splitlines() if '=' in line)
                    encoded = values.get('X-DeskPort-Executable')
                    try:
                        matches_identity = encoded and base64.b64decode(encoded, validate=True).decode() == str(identity)
                    except (ValueError, UnicodeDecodeError):
                        matches_identity = False
                    if matches_identity:
                        matching.append((entry, values))
                assert len(matching) == 1, f'KWin permission Exec does not match /proc identity: {identity}; {entries}'
                entry, values = matching[0]
                expected = dict(line.split('=', 1) for line in permission_entry(identity).splitlines() if '=' in line)
                assert values.get('Exec') == expected['Exec'], (identity, values)
                assert 'zkde_screencast_unstable_v1' in values.get('X-KDE-Wayland-Interfaces', '').split(';'), values
                evidence.setdefault('permissionEntries', []).append({'path': str(entry), 'content': entry.read_text()})
            seq = 0
            for cycle in range(args.cycles):
                for width, height, scale in ((2560, 1440, 1), (1668, 1888, 2), (1764, 1888, 2)):
                    seq += 1
                    ack = send(display, json.dumps({'displayPolicy': policy, 'seq': seq, 'width': width, 'height': height, 'scale': scale, 'session': True}))
                    assert {key: ack[key] for key in ('seq', 'width', 'height', 'scale')} == {'seq': seq, 'width': width, 'height': height, 'scale': scale}, ack
                    owned = ack['outputName']
                    snapshot(f'policy-{policy}-cycle-{cycle}-resize-{width}x{height}', qt, gui)
                    verify_recovery(display)
                    current = outputs(True)
                    virtual = current.pop(owned)
                    assert (virtual['width'], virtual['height'], virtual['scale']) == (width, height, scale), virtual
                    assert virtual['priority'] == 1 and not virtual['replication_source'] and (virtual['x'], virtual['y']) == (0, 0), virtual
                    assert set(current) == set(baseline_devices), current
                    for name, original in baseline_devices.items():
                        actual = current[name]
                        if policy == 0:
                            assert actual['replication_source'] == virtual['uuid'] and actual['priority'] > 1, actual
                        else:
                            assert not actual['enabled'], actual
                        for key in ('width', 'height', 'refresh', 'scale', 'transform', 'x', 'y', 'enabled'):
                            if policy == 1 and key == 'enabled':
                                continue
                            assert actual[key] == original[key], (name, key, original, actual)
                    assert set(outputs()) == {owned}, 'Virtual-primary layout still exposes an extended workspace'
                seq += 1
                send(display, json.dumps({'displayPolicy': policy, 'seq': seq, 'width': 1280, 'height': 720, 'scale': 1, 'session': False}))
                snapshot(f'policy-{policy}-cycle-{cycle}-disconnect', qt, gui)
                assert outputs() == baseline and outputs(True) == baseline_devices, 'Disconnect did not restore baseline'
            display.stdin.close()
            assert display.wait(timeout=8) == 0
            snapshot(f'policy-{policy}-helper-eof', qt, gui)
            assert outputs() == baseline and outputs(True) == baseline_devices, 'Helper EOF did not restore baseline'
        final = snapshot('finished', qt, gui)
        assert final['screenEvents'] > 0 and final['frames'] > initial['frames'], final
        evidence['passed'] = True
        print('PASS bundled Qt Wayland, persistent QOffscreenSurface and AppRun GUI; policy 0/1 resize/disconnect/reconnect; nonzero wl_output; restored local outputs', flush=True)
        if not args.seed_permission:
            print('PASS actual helper /proc identity and automatically installed KWin Exec permission', flush=True)
    except BaseException as error:
        evidence['failure'] = f'{type(error).__name__}: {error}'
        for log in logs:
            log.flush()
            log.seek(0)
            print(f'LOG {log.name}\n{log.read()[-7000:]}', file=sys.stderr)
        raise
    finally:
        for child in reversed(children):
            stop(child)
        evidence['teardown'] = [{'pid': child.pid, 'status': child.poll(), 'procExists': Path(f'/proc/{child.pid}').exists()} for child in children]
        (results / 'display-session.json').write_text(json.dumps(evidence, indent=2) + '\n')
        for log in logs:
            log.close()


def main():
    args = arguments()
    if args.build_probe:
        build_probe(args)
    elif not args.inside:
        outer(args)
    else:
        def interrupted(signum, frame):
            raise KeyboardInterrupt(f'signal {signum}')
        signal.signal(signal.SIGTERM, interrupted)
        inside(args)


if __name__ == '__main__':
    main()
