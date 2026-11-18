#!/usr/bin/env python3
"""Run retained release lifecycle scenarios inside an owned distro-lab guest.

This is a guest command, not a host installer. Inputs are a single explicit tar
archive containing manifest.json and checksum-locked release packages. Output is
JSON on stdout so the managed lab runner retains it with VM health and teardown.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import argparse
import configparser
import contextlib
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import select
import signal
import subprocess
import tarfile
import time


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def capture_selection_verified(backend, text, identity, baseline):
    """A config echo/output inventory alone cannot prove the selected capture."""
    name = identity['outputName']
    size = f"{identity['width']}x{identity['height']}"
    if backend == 'kwin':
        return bool(re.search(r'\[kwingrab\] Screencasting output name ' + re.escape(name)
                              + r' position [^\n]* resolution ' + re.escape(size), text))
    if backend == 'portal':
        return f'DeskPort owned virtual capture: {name} {size}' in text
    if backend == 'wlr':
        # WLR logs its actual buffer size after enumerating every output. Keep
        # the fixture size distinct from original outputs to disambiguate it.
        distinct = baseline is not None and all(
            (o.get('width'), o.get('height')) != (identity['width'], identity['height']) for o in baseline)
        sizes = re.findall(r'\[wlgrab\] Resolution: ([0-9]+x[0-9]+)', text)
        return bool(distinct and sizes and set(sizes) == {size}
                    and re.search(r'\[wlgrab\] Monitor [0-9]+ is ' + re.escape(name) + ':', text))
    return False


def run(argv, **kwargs):
    return subprocess.run(argv, text=True, capture_output=True, timeout=180,
                          check=True, **kwargs).stdout.strip()


def identity(pid):
    try:
        fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
        return fields[19] if fields[0] != 'Z' else None
    except (FileNotFoundError, ProcessLookupError):
        return None


def owned_processes(root, appimage=None):
    found = {root}
    for _ in range(8):
        children = set()
        for pid in found:
            try:
                children.update(map(int, Path(f'/proc/{pid}/task/{pid}/children').read_text().split()))
            except FileNotFoundError:
                pass
        found |= children
    if appimage:
        # The FUSE runtime may daemonize. Match this exact synthetic installed
        # path, never an application name or an unrelated mount.
        for path in Path('/proc').iterdir():
            if not path.name.isdigit():
                continue
            try:
                if path.stat().st_uid != os.getuid():
                    continue
                env = (path / 'environ').read_bytes().split(b'\0')
                if os.fsencode('APPIMAGE=' + appimage) in env or (path / 'exe').resolve() == Path(appimage):
                    found.add(int(path.name))
            except (FileNotFoundError, ProcessLookupError, PermissionError):
                pass
    return {pid: identity(pid) for pid in found if identity(pid)}


def apt_install(paths):
    cached = []
    for path in paths:
        cache_path = Path('/var/cache/apt/archives') / path.name
        run(['sudo', '-n', 'install', '-m', '644', str(path), str(cache_path)])
        cached.append(str(cache_path))
    run(['sudo', '-n', 'apt-get', '-y', '--allow-downgrades',
         '-o', 'Acquire::Retries=0', '-o', 'Acquire::http::Timeout=5',
         '-o', 'Acquire::https::Timeout=5', 'install'] + cached)


@contextlib.contextmanager
def x11_session(controller):
    """Temporarily select an already installed X11 desktop in this guest only."""
    sessions = sorted(Path('/usr/share/xsessions').glob('*plasma*.desktop'))
    assert sessions, 'No installed Plasma X11 session; do not substitute Xwayland'
    assert shutil.which('kwin_x11') and shutil.which('xprop') and shutil.which('xwininfo'), 'X11 window manager/tools missing'
    target = Path('/etc/sddm.conf.d/99-deskport-lifecycle-session.conf')
    assert target.parent.is_dir() and not target.exists(), 'Unsupported or already owned display manager'
    config = controller / 'sddm-x11.conf'
    config.write_text('[Autologin]\nUser=tester\nSession=' + sessions[0].name + '\nRelogin=true\n')

    def wait_session(kind):
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            ids = run(['loginctl', 'show-user', 'tester', '-p', 'Sessions', '--value']).split()
            types = []
            for session_id in ids:
                result = subprocess.run(['loginctl', 'show-session', session_id, '-p', 'Type', '-p', 'Active'],
                    capture_output=True, text=True, timeout=10)
                if result.returncode == 0:
                    properties = dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)
                    if properties.get('Active') == 'yes':
                        types.append(properties.get('Type'))
            if kind in types:
                for path in Path('/proc').iterdir():
                    if not path.name.isdigit():
                        continue
                    try:
                        expected = 'kwin_x11' if kind == 'x11' else 'kwin_wayland'
                        if path.stat().st_uid == os.getuid() and (path / 'comm').read_text().strip().startswith(expected):
                            if kind == 'wayland':
                                return dict(os.environ)  # Final lab health checks its live socket/environment.
                            # KWin may be non-dumpable after its normal realtime
                            # capabilities. Read only this synthetic guest's X11
                            # connection metadata; do not change those capabilities.
                            values = subprocess.check_output(['sudo', '-n', 'cat', str(path / 'environ')], timeout=10).split(b'\0')
                            env = dict(os.environ)
                            selected = dict(os.fsdecode(v).split('=', 1) for v in values if b'=' in v)
                            for key in ('DISPLAY', 'XAUTHORITY', 'XDG_CURRENT_DESKTOP', 'XDG_RUNTIME_DIR', 'DBUS_SESSION_BUS_ADDRESS'):
                                if key in selected:
                                    env[key] = selected[key]
                            if kind == 'x11':
                                env.pop('WAYLAND_DISPLAY', None)
                                env['XDG_SESSION_TYPE'] = 'x11'
                                subprocess.run(['xprop', '-root'], env=env, capture_output=True, check=True, timeout=10)
                            return env
                    except (FileNotFoundError, ProcessLookupError, PermissionError, subprocess.CalledProcessError):
                        pass
            time.sleep(2)
        raise TimeoutError(f'Guest did not enter a real {kind} session')

    run(['sudo', '-n', 'install', '-m', '644', str(config), str(target)])
    try:
        run(['sudo', '-n', 'systemctl', 'restart', 'sddm'])
        yield wait_session('x11')
    finally:
        run(['sudo', '-n', 'rm', str(target)])
        run(['sudo', '-n', 'systemctl', 'restart', 'sddm'])
        wait_session('wayland')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('--session', choices=('wayland', 'x11'), default='wayland')
    parser.add_argument('--inside-session', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    def timeout(*_):
        raise TimeoutError('Guest lifecycle exceeded 20 minutes')
    signal.signal(signal.SIGALRM, timeout)
    signal.alarm(1200)
    assert os.getuid() == 2000, 'Only the lab tester account is allowed'
    assert run(['systemd-detect-virt']) in ('kvm', 'qemu'), 'A full isolated VM is required'
    # The runner exports the generic desktop environment. Native Hyprland IPC
    # also needs its explicitly selected instance, without importing unrelated
    # user-manager variables or credentials.
    for line in run(['systemctl', '--user', 'show-environment']).splitlines():
        key, _, value = line.partition('=')
        if key in ('HYPRLAND_INSTANCE_SIGNATURE', 'XAUTHORITY'):
            os.environ[key] = value
    if args.session == 'x11' and not args.inside_session:
        controller = Path.home() / '.local/state/deskport-lifecycle-controller'
        controller.mkdir(parents=True, exist_ok=False)
        with tarfile.open(args.bundle) as archive:
            script = controller / 'scenario.py'
            script.write_bytes(archive.extractfile('scenario.py').read())
            manifest = json.load(archive.extractfile('manifest.json'))
            dependencies = []
            for item in manifest.get('dependencies', []):
                assert Path(item['name']).name == item['name'] and item['name'].endswith('.deb')
                path = controller / item['name']
                path.write_bytes(archive.extractfile(item['name']).read())
                assert sha(path) == item['sha256']
                dependencies.append(path)
            if dependencies:
                apt_install(dependencies)
        with x11_session(controller) as session_env:
            result = subprocess.run([os.sys.executable, str(script), str(args.bundle),
                '--session', 'x11', '--inside-session'], env=session_env, timeout=1000)
        assert result.returncode == 0, 'X11 lifecycle failed; Wayland desktop restored'
        return
    assert os.environ.get('WAYLAND_DISPLAY') if args.session == 'wayland' else os.environ.get('DISPLAY'), 'Managed guest desktop missing'
    assert os.environ.get('XDG_SESSION_TYPE') == args.session, 'Actual desktop session type mismatch'
    if args.session == 'x11':
        run(['pgrep', '-x', 'Xorg'])
        run(['pgrep', '-x', 'kwin_x11'])
    assert not Path('/opt/deskport').exists(), 'Baseline already contains DeskPort'
    state = Path.home() / '.local/state/deskport-lifecycle'
    state.mkdir(parents=True, exist_ok=False)
    with tarfile.open(args.bundle) as archive:
        archive.extractall(state / 'inputs', filter='data')
    inputs = state / 'inputs'
    manifest = json.loads((inputs / 'manifest.json').read_text())
    for item in manifest['packages'] + manifest.get('dependencies', []):
        assert Path(item['name']).name == item['name']
        assert sha(inputs / item['name']) == item['sha256'], item
    if manifest.get('media'):
        media = manifest['media']
        runtime = inputs / 'media-runtime.tar.gz'
        assert sha(runtime) == media['runtime_sha256']
        for name, checksum in media['files'].items():
            path = Path(name)
            assert not path.is_absolute() and '..' not in path.parts
            assert sha(inputs / 'media' / path) == checksum
        with tarfile.open(runtime) as archive:
            assert all(m.name.startswith('nix/store/') and '..' not in Path(m.name).parts for m in archive.getmembers())
        run(['sudo', '-n', 'tar', '-xzf', str(runtime), '-C', '/'])
    if manifest.get('debugger'):
        debugger = manifest['debugger']
        archive_path = inputs / debugger['name']
        assert sha(archive_path) == debugger['sha256']
        with tarfile.open(archive_path) as archive:
            assert all(m.name.startswith('nix/store/') and '..' not in Path(m.name).parts for m in archive.getmembers())
        run(['sudo', '-n', 'tar', '-xzf', str(archive_path), '-C', '/'])
    env = dict(os.environ, XDG_CONFIG_HOME=str(state / 'config'),
               XDG_DATA_HOME=str(state / 'data'), XDG_CACHE_HOME=str(state / 'cache'),
               QT_QPA_PLATFORM='wayland' if args.session == 'wayland' else 'xcb', QT_QUICK_BACKEND='software',
               QSG_RHI_BACKEND='software', LIBGL_ALWAYS_SOFTWARE='1')
    for key in ('XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'XDG_CACHE_HOME'):
        Path(env[key]).mkdir()
    sentinel = Path(env['XDG_CONFIG_HOME']) / 'deskport-retained-synthetic.txt'
    sentinel.write_text('DeskPort synthetic retention fixture\n')
    sentinel_sha = sha(sentinel)
    settings = Path(env['XDG_CONFIG_HOME']) / 'DeskPort/DeskPort.conf'
    settings.parent.mkdir()
    settings.write_text('[LifecycleFixture]\ntoken=synthetic-retained-value\n')
    report = dict(schema=1, manifest=manifest, desktop=os.environ.get('XDG_CURRENT_DESKTOP'),
                  session=os.environ.get('XDG_SESSION_TYPE'), scenarios=[], passed=False,
                  test_kind=('va-runtime-regression' if manifest.get('verify_va_runtime_fix') else
                             'synthetic-media' if manifest.get('media') else
                             'lifecycle-capture-probe' if manifest.get('capture_probe') else 'lifecycle'),
                  limits=['Software virtual GPU only', 'No personal peers or clipboard',
                          'Wayland activation transport checked; X11 also checks viewability; focus unverified',
                          'No native OS input or physical hardware acceptance',
                          ('Synthetic media uses a separate Xvfb/private Pulse fixture, not Wayland video transport'
                           if manifest.get('media') else 'No sustained media connection')])
    if shutil.which('getenforce'):
        report['selinux_before'] = run(['getenforce'])
    portal_probe = (['gdbus', 'introspect', '--session', '--dest', 'org.freedesktop.portal.Desktop',
                     '--object-path', '/org/freedesktop/portal/desktop'] if shutil.which('gdbus') else
                    ['busctl', '--user', 'introspect', 'org.freedesktop.portal.Desktop',
                     '/org/freedesktop/portal/desktop'])
    report['prerequisites'] = {
        'uinput_exists': Path('/dev/uinput').exists(),
        'uinput_writable': os.access('/dev/uinput', os.W_OK),
        'portal_probe': portal_probe[0],
        'portal': subprocess.run(portal_probe,
            text=True, capture_output=True, timeout=20).returncode == 0,
        'pipewire': subprocess.run(['systemctl', '--user', 'is-active', 'pipewire'],
            capture_output=True, timeout=20).returncode == 0,
    }
    clipboard = dict(content_read=False, advertised_only=True, protocols=[], probe='unavailable')
    if args.session == 'wayland' and shutil.which('wayland-info'):
        try:
            probe = subprocess.run(['wayland-info'], text=True, capture_output=True, timeout=15)
            clipboard.update(probe='wayland-info', exit_code=probe.returncode,
                protocols=[name for name in ('wl_data_device_manager', 'zwlr_data_control_manager_v1',
                    'ext_data_control_manager_v1', 'zwp_primary_selection_device_manager_v1')
                    if re.search(r"interface: ['\"]?" + re.escape(name) + r"['\"]?[,\s]", probe.stdout)])
        except subprocess.TimeoutExpired:
            clipboard.update(probe='wayland-info', failure='timeout')
    report['prerequisites']['clipboard'] = clipboard

    def display_lease(gui):
        # AppImage descendants carry the runtime selected by the real launcher.
        # Reuse it for the bundled helper, as production does.
        processes = {gui.pid}
        for _ in range(8):
            children = set()
            for pid in processes:
                try:
                    children.update(map(int, Path(f'/proc/{pid}/task/{pid}/children').read_text().split()))
                except FileNotFoundError:
                    pass
            processes |= children
        selected = None
        for pid in processes:
            try:
                values = Path(f'/proc/{pid}/environ').read_bytes().split(b'\0')
                candidate = dict(os.fsdecode(v).split('=', 1) for v in values if b'=' in v)
                if candidate.get('APPDIR') and Path(candidate['APPDIR'], 'usr/bin/deskport-display').exists():
                    selected = candidate
            except FileNotFoundError:
                pass
        assert selected, 'Running AppImage runtime not found'
        selected.pop('SHARUN_EXTRA_LIBRARY_PATH', None)
        # KWin reads permissions from its own guest user's data/cache roots.
        # A private GUI XDG data root would be invisible to that compositor.
        # This remains the disposable tester home, never the builder's desktop.
        selected['XDG_DATA_HOME'] = os.environ.get('XDG_DATA_HOME', str(Path.home() / '.local/share'))
        selected['XDG_CACHE_HOME'] = os.environ.get('XDG_CACHE_HOME', str(Path.home() / '.cache'))
        selected['DESKPORT_DISPLAY_ON_DEMAND'] = '1'
        helper = Path(selected['APPDIR']) / 'usr/bin/deskport-display'

        def topology():
            if shutil.which('kscreen-doctor'):
                value = json.loads(run(['kscreen-doctor', '-j']))
                return [{k: o.get(k) for k in ('name', 'enabled', 'connected', 'pos', 'scale',
                                               'rotation', 'currentModeId')} for o in value['outputs']]
            if shutil.which('hyprctl'):
                value = json.loads(run(['hyprctl', '-j', 'monitors', 'all']))
                return [{k: o.get(k) for k in ('name', 'width', 'height', 'x', 'y', 'scale',
                                               'disabled')} for o in value]
            return None

        baseline = topology()
        def capture_probe(identity):
            # Same production capture/encoder gate as test-linux-display.py,
            # using only this disposable guest's owned virtual output.
            probes = report.setdefault('capture_probes', [])
            case = state / ('capture-' + str(len(probes)))
            case.mkdir()
            descriptor = case / 'virtual-display.json'
            descriptor.write_text(json.dumps(dict(node=identity.get('pipewireNode'),
                serial=identity.get('pipewireSerial'), output=identity['outputName'],
                width=identity['width'], height=identity['height'], scale=identity['scale'])))
            desktop = os.environ.get('XDG_CURRENT_DESKTOP', '').upper()
            backend = 'portal' if 'GNOME' in desktop else ('wlr' if shutil.which('hyprctl') else 'kwin')
            config = case / 'sunshine.conf'
            config.write_text('\n'.join([
                'capture = ' + backend, 'output_name = ' + identity['outputName'],
                'encoder = software', 'keyboard = disabled', 'mouse = disabled',
                'controller = disabled', 'stream_audio = disabled', 'upnp = disabled',
                'system_tray = disabled', 'bind_address = 127.0.0.1', 'address_family = ipv4',
                'port = ' + str(56989 + 100 * len(probes)), 'min_log_level = 1',
                *[key + ' = ' + str(case / filename) for key, filename in (
                    ('file_state', 'state.json'), ('credentials_file', 'control.json'),
                    ('pkey', 'key.pem'), ('cert', 'cert.pem'), ('file_apps', 'apps.json'),
                    ('log_path', 'sunshine.log'))], '']))
            (case / 'apps.json').write_text('{"apps": []}')
            hostenv = dict(selected, DESKPORT_VIRTUAL_DISPLAY=str(descriptor),
                DBUS_SYSTEM_BUS_ADDRESS='unix:path=' + str(case / 'no-system-bus'))
            for key in ('DESKPORT_ON_DEMAND_DISPLAY', 'DESKPORT_DISPLAY_ON_DEMAND'):
                hostenv.pop(key, None)
            host = Path(selected['APPDIR']) / 'usr/libexec/deskport-host'
            with (case / 'probe.log').open('w+') as log:
                process = subprocess.Popen([str(host), str(config)], env=hostenv,
                    cwd=case, stdout=log, stderr=log, start_new_session=True)
                text = ''
                try:
                    deadline = time.monotonic() + 25
                    while time.monotonic() < deadline:
                        log.seek(0)
                        text = log.read()
                        if 'Found H.264 encoder:' in text or process.poll() is not None:
                            break
                        time.sleep(.1)
                finally:
                    if process.poll() is None:
                        os.killpg(process.pid, signal.SIGTERM)
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            os.killpg(process.pid, signal.SIGKILL)
                            process.wait(timeout=5)
                    log.seek(0)
                    text = log.read()
            selection = capture_selection_verified(backend, text, identity, baseline)
            passed = 'Found H.264 encoder:' in text and selection
            probes.append(dict(passed=passed, backend=backend, identity=identity,
                selected_output_verified=selection, encoder='software', log=text,
                media_connection=False, input_enabled=False))
            assert passed, 'Owned virtual capture/software encoder probe failed; see capture_probes'

        with (state / 'display-helper.log').open('w') as log:
            child = subprocess.Popen([str(helper), '1280', '720'], env=selected,
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log, text=True, start_new_session=True)
            replies = []
            try:
                def receive():
                    assert select.select([child.stdout], [], [], 20)[0], 'Display helper timeout'
                    line = child.stdout.readline()
                    assert line, (state / 'display-helper.log').read_text()
                    value = json.loads(line)
                    assert 'error' not in value, value
                    replies.append(value)
                    return value
                ready = receive()
                assert ready.get('ready') and not ready.get('active'), ready
                assert topology() == baseline
                policy = 0 if ready.get('backend') == 'hyprland' else 2
                for seq, active in enumerate((True, False, True, False), 1):
                    child.stdin.write(json.dumps(dict(displayPolicy=policy, seq=seq, width=1280,
                        height=720, scale=1, session=active)) + '\n')
                    child.stdin.flush()
                    reply = receive()
                    assert reply.get('active') == active, reply
                    if active and manifest.get('capture_probe'):
                        capture_probe(reply)
                    if not active:
                        assert topology() == baseline, 'Desktop topology was not restored'
                child.stdin.close()
                assert child.wait(timeout=15) == 0
                report['display_lease'] = dict(passed=True, replies=replies, baseline=baseline,
                    topology_observed=baseline is not None, cycles=2, policy=policy,
                    backend=ready.get('backend'))
                if manifest.get('media'):
                    tools = manifest['media']['tools']
                    media_root = inputs / 'media'
                    media_env = dict(os.environ, DESKPORT_APPIMAGE_RUNTIME='private')
                    for key in ('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML_IMPORT_PATH',
                                'QML2_IMPORT_PATH', 'LD_LIBRARY_PATH'):
                        media_env.pop(key, None)
                    media_env['PATH'] = ':'.join(str(Path(v).parent) for v in tools.values()) + ':' + media_env['PATH']
                    command = [os.sys.executable, str(media_root / 'tests/test-browser-native.py'),
                        '--host', str(Path(selected['APPDIR']) / 'usr/libexec/deskport-host'),
                        '--gateway', str(media_root / 'browser-live-gateway'),
                        '--chromium', tools['chromium'], '--pulse-prefix', str(Path(tools['pulseaudio']).parent.parent),
                        '--node', tools['node'], '--playwright-module', str(media_root / 'playwright-core'),
                        '--openssl', tools['openssl'], '--output', str(state / 'media-result'),
                        '--runtime-root', str(state / 't')]
                    with (state / 'media-fixture.log').open('w') as log:
                        result = subprocess.run(command, env=media_env, stdout=log, stderr=subprocess.STDOUT, timeout=360)
                    report['synthetic_media'] = json.loads((state / 'media-result/native-fixture-result.json').read_text())
                    path = state / 'media-result/browser-result.json'
                    if path.exists():
                        report['synthetic_media']['browser_result'] = json.loads(path.read_text())
                    if result.returncode:
                        report['synthetic_media']['fixture_log'] = (state / 'media-fixture.log').read_text()[-8000:]
                    assert result.returncode == 0 and report['synthetic_media']['ownedProcessesStopped'], 'Synthetic AV fixture failed'
                    (state / 'progress.json').write_text(json.dumps(dict(stage='synthetic-media-complete')))
                    time.sleep(12)  # Let the owned host observer retain synthetic screenshots.
            finally:
                if child.poll() is None:
                    os.killpg(child.pid, signal.SIGTERM)
                    child.wait(timeout=15)

    def retained():
        assert sentinel.exists() and sha(sentinel) == sentinel_sha
        config = configparser.ConfigParser()
        config.read(settings)
        assert config['LifecycleFixture']['token'] == 'synthetic-retained-value'

    def smoke(command, version, label):
        def x11_windows():
            windows = re.findall(r'0x[0-9a-fA-F]+', run(['xprop', '-root', '_NET_CLIENT_LIST'], env=env))
            matching = [w for w in windows if 'deskport' in run(['xprop', '-id', w, 'WM_CLASS'], env=env).lower()]
            return [w for w in matching if 'IsViewable' in run(['xwininfo', '-id', w, '-stats'], env=env)]

        def diagnose():
            if not manifest.get('debugger') or not label.startswith('deb-'):
                return
            argv = ['timeout', '-s', 'INT', '-k', '3', '20', manifest['debugger']['path'],
                    '-nh', '-nx', '--batch', '--quiet', '-ex', 'set confirm off',
                    '-ex', 'set debuginfod enabled off', '-ex', 'run', '-ex', 'thread apply all bt',
                    '--args', '/bin/sh'] + command + ['--background', '--no-host-autostart', '--no-web-server']
            with (state / (label + '-gdb.log')).open('w+') as log:
                diagnostic = subprocess.Popen(argv, env=env, stdout=log, stderr=log, start_new_session=True)
                try:
                    diagnostic.wait(timeout=30)
                finally:
                    if diagnostic.poll() is None:
                        os.killpg(diagnostic.pid, signal.SIGKILL)
                        diagnostic.wait(timeout=5)
                log.seek(0)
                report.setdefault('debugger_traces', []).append(dict(stage=label, output=log.read()))

        version_output = run(command + ['--version'], env=env)
        assert f'DeskPort {version}' in version_output, version_output
        logpath = state / (label + '.log')
        with logpath.open('w') as log:
            child = subprocess.Popen(command + ['--background', '--no-host-autostart', '--no-web-server'],
                                     env=env, stdout=log, stderr=log, start_new_session=True)
            try:
                time.sleep(5)
                if child.poll() is not None:
                    diagnose()
                assert child.poll() is None, f'GUI exited ({child.poll()}): ' + logpath.read_text()
                if args.session == 'x11':
                    assert not x11_windows(), 'Background launch mapped a visible X11 window'
                run(command + ['--no-host-autostart', '--no-web-server'], env=env)
                time.sleep(2)
                if child.poll() is not None:
                    diagnose()
                assert child.poll() is None, f'Activation owner exited ({child.poll()})'
                if manifest.get('verify_va_runtime_fix') and label == 'deb-upgrade':
                    (state / 'progress.json').write_text(json.dumps(dict(stage='candidate-warmup', pid=child.pid)))
                    observed = time.monotonic()
                    announced = False
                    while time.monotonic() - observed < 30:
                        assert child.poll() is None, f'Candidate exited during observation ({child.poll()})'
                        if not announced and time.monotonic() - observed >= 10:
                            (state / 'progress.json').write_text(json.dumps(dict(stage='candidate-observation', pid=child.pid)))
                            announced = True
                        time.sleep(.25)
                    report['candidate_observation_seconds'] = round(time.monotonic() - observed, 2)
                    metadata = json.loads(Path('/opt/deskport/usr/share/doc/deskport/libva-runtime/source.json').read_text())
                    assert metadata['source_version'] == '2.23.0'
                    mapped = set()
                    for line in Path(f'/proc/{child.pid}/maps').read_text().splitlines():
                        fields = line.split(None, 5)
                        if len(fields) == 6 and fields[5].startswith('/'):
                            mapped.add(str(Path(fields[5]).resolve()))
                    verified = {}
                    for item in metadata['runtime_files']:
                        path = Path('/opt/deskport') / item['path']
                        assert str(path.resolve()) in mapped, f'Candidate did not map {path}'
                        checksum = sha(path)
                        assert 'SHA256:' + checksum == item['checksum'], path
                        verified[item['path']] = checksum
                    assert len(verified) == 4
                    report['candidate_va_runtime'] = dict(source_version=metadata['source_version'], mapped_files=verified)
                    (state / 'progress.json').write_text(json.dumps(dict(stage='candidate-observation-complete')))
                if args.session == 'x11':
                    matching = x11_windows()
                    assert matching, 'Recall did not map a DeskPort X11 window'
                    report.setdefault('x11_recall', []).append(dict(stage=label, windows=matching,
                        initially_hidden=True, viewable=True))
                if label == 'appimage-upgrade' and args.session == 'wayland':
                    try:
                        display_lease(child)
                        report['scenarios'].append(dict(name='appimage-display-lease', passed=True))
                    except Exception as error:
                        report['scenarios'].append(dict(name='appimage-display-lease', passed=False, failure=repr(error)))
            finally:
                owned = owned_processes(child.pid, command[0] if command[0].endswith('.AppImage') else None)
                if child.poll() is None:
                    os.killpg(child.pid, signal.SIGTERM)
                    try:
                        child.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        os.killpg(child.pid, signal.SIGKILL)
                        child.wait(timeout=10)
                for sig in (signal.SIGTERM, signal.SIGKILL):
                    for pid, started in owned.items():
                        if identity(pid) == started:
                            os.kill(pid, sig)
                    deadline = time.monotonic() + 5
                    while any(identity(pid) == started for pid, started in owned.items()) and time.monotonic() < deadline:
                        time.sleep(.1)
                assert not any(identity(pid) == started for pid, started in owned.items()), 'Owned process survived cleanup'
            assert child.poll() is not None
        text = logpath.read_text()
        for failure in ('QQmlApplicationEngine failed', 'Could not load the Qt platform plugin',
                        'error while loading shared libraries'):
            assert failure not in text, text
        retained()
        report['scenarios'].append(dict(name=label, version=version, passed=True,
            background_owner_survived_activation=True, log=text))

    try:
        for route in manifest['routes']:
            if route == 'flatpak':
                # A separate installation and synthetic guest home keep host and
                # viewer settings apart. The system runtime is read-only input.
                env['FLATPAK_USER_DIR'] = str(state / 'flatpak-installation')
                settings = Path.home() / '.var/app/io.github.keithxc.DeskPort/config/DeskPort/DeskPort.conf'
                settings.parent.mkdir(parents=True, exist_ok=True)
                settings.write_text('[LifecycleFixture]\ntoken=synthetic-retained-value\n')
                for runtime in manifest.get('runtimes', [manifest['runtime']] if manifest.get('runtime') else []):
                    assert Path(runtime['name']).name == runtime['name']
                    assert sha(inputs / runtime['name']) == runtime['sha256']
                    run(['flatpak', '--user', 'install', '--noninteractive', '--no-deps', '--no-related',
                         str(inputs / runtime['name'])], env=env)
                    commit = run(['flatpak', '--user', 'info', '--show-commit', runtime['pin']['ref']], env=env)
                    assert commit == runtime['pin']['commit']
                report['flatpak_runtimes'] = run(['flatpak', 'list', '--runtime', '--columns=ref'], env=env)
            packages = [p for p in manifest['packages'] if p['route'] == route]
            old, new = (next(p for p in packages if p['role'] == role) for role in ('old', 'new'))
            for label, package in [('install', old), ('upgrade', new), ('rollback', old)]:
                artifact = inputs / package['name']
                if route == 'appimage':
                    installed = state / 'installed/DeskPort.AppImage'
                    installed.parent.mkdir(exist_ok=True)
                    candidate = installed.with_suffix('.next')
                    shutil.copy2(artifact, candidate)
                    candidate.chmod(0o700)
                    candidate.replace(installed)
                    command = [str(installed)]
                elif route == 'deb':
                    local = [artifact] + [inputs / p['name'] for p in manifest.get('dependencies', [])]
                    # All artifacts are local. The runner still restricts guest
                    # egress; no repository or network policy is changed.
                    apt_install(local)
                    command = ['/usr/bin/deskport']
                elif route == 'rpm':
                    run(['sudo', '-n', 'rpm', '-U', '--oldpackage', str(artifact)])
                    command = ['/usr/bin/deskport']
                elif route == 'arch':
                    run(['sudo', '-n', 'pacman', '-U', '--noconfirm', str(artifact)])
                    command = ['/usr/bin/deskport']
                elif route == 'flatpak':
                    run(['flatpak', '--user', 'install', '--noninteractive', '--no-deps', '--no-related',
                         '--reinstall', str(artifact)], env=env)
                    command = ['flatpak', 'run', '--env=QT_QUICK_BACKEND=software',
                               '--env=LIBGL_ALWAYS_SOFTWARE=1', 'io.github.keithxc.DeskPort']
                    run(['flatpak', 'run', '--command=sh', 'io.github.keithxc.DeskPort', '-ec',
                         'test ! -e /app/opt/deskport/usr/libexec; test ! -e /app/bin/sunshine'], env=env)
                    report['flatpak_permissions'] = run(['flatpak', '--user', 'info',
                        '--show-permissions', 'io.github.keithxc.DeskPort'], env=env)
                    permissions = configparser.ConfigParser(interpolation=None)
                    permissions.read_string(report['flatpak_permissions'])
                    assert not permissions.get('Context', 'filesystems', fallback='').strip()
                    assert not permissions.has_option('Session Bus Policy', 'org.freedesktop.Flatpak')
                    report['flatpak_viewer_only'] = True
                else:
                    raise ValueError(f'Unsupported route {route}')
                if manifest.get('package_api') and label == 'upgrade' and route in ('deb', 'rpm', 'arch'):
                    report['package_api'] = run(['python3', str(inputs / 'check-linux-package.py'),
                        '/opt/deskport', package['version']], env=env)
                try:
                    smoke(command, package['version'], route + '-' + label)
                except Exception as error:
                    failed = dict(name=route + '-' + label, passed=False,
                        failure=repr(error), stdout=getattr(error, 'stdout', None),
                        stderr=getattr(error, 'stderr', None))
                    if manifest.get('verify_va_runtime_fix') and package['role'] == 'old':
                        traces = [t['output'] for t in report.get('debugger_traces', []) if t['stage'] == failed['name']]
                        failed['expected_failure'] = any(all(marker in trace for marker in
                            ('received signal SIGSEGV', 'libva-wayland.so.2', 'vaInitialize')) for trace in traces)
                    report['scenarios'].append(failed)
                else:
                    if manifest.get('verify_va_runtime_fix') and package['role'] == 'new':
                        assert report['candidate_va_runtime']['source_version'] == '2.23.0'
                    if manifest.get('verify_va_runtime_fix') and package['role'] == 'old':
                        report['scenarios'][-1].update(passed=False, failure='Legacy VA crash negative control did not reproduce')
            if route == 'appimage':
                installed.unlink()
                assert not installed.exists()
            elif route == 'flatpak':
                run(['flatpak', '--user', 'uninstall', '--noninteractive',
                     'io.github.keithxc.DeskPort'], env=env)
                refs = run(['flatpak', '--user', 'list', '--app', '--columns=application'], env=env)
                assert 'io.github.keithxc.DeskPort' not in refs
            else:
                remove = {'deb': ['dpkg', '-r', 'deskport'], 'rpm': ['rpm', '-e', 'deskport'],
                          'arch': ['pacman', '-R', '--noconfirm', 'deskport']}[route]
                run(['sudo', '-n'] + remove)
                assert not Path('/usr/bin/deskport').exists()
                assert not Path('/opt/deskport/AppRun').exists()
            retained()
            report['scenarios'].append(dict(name=route + '-uninstall-retain-config', passed=True))
        if 'selinux_before' in report:
            report['selinux_after'] = run(['getenforce'])
            assert report['selinux_before'] == report['selinux_after']
        report['passed'] = all(item['passed'] or item.get('expected_failure') for item in report['scenarios'])
        assert report['passed'], 'One or more lifecycle scenarios failed; see per-stage evidence'
    except BaseException as error:
        report['failure'] = repr(error)
        if isinstance(error, subprocess.CalledProcessError):
            report['command_stdout'] = error.stdout
            report['command_stderr'] = error.stderr
        raise
    finally:
        retained()
        (state / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2), flush=True)
        signal.alarm(0)


if __name__ == '__main__':
    main()
