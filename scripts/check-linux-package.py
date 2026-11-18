#!/usr/bin/env python3
"""Exercise a packaged viewer and host without a real desktop or personal state."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import base64
import hashlib
import json
import os
import ssl
import time
import urllib.error
import urllib.request
import pathlib
import signal
import subprocess
import sys
import tempfile

root = pathlib.Path(sys.argv[1]).resolve()
version = sys.argv[2]
for prefix in (root / 'usr', root / 'usr/libexec/sunshine/usr'):
    metadata_path = prefix / 'share/doc/deskport/libva-runtime/source.json'
    if metadata_path.exists():
        metadata = json.loads(metadata_path.read_text())
        assert metadata['source_version'] == '2.23.0'
        assert (metadata_path.parent / 'COPYING').is_file()
        assert {item['path'] for item in metadata['runtime_files']} == {
            'usr/lib/' + name for name in ('libva.so.2', 'libva-drm.so.2', 'libva-x11.so.2', 'libva-wayland.so.2')}
        for item in metadata['runtime_files']:
            path = prefix.parent / item['path']
            assert path.stat().st_size == item['bytes']
            assert 'SHA256:' + hashlib.sha256(path.read_bytes()).hexdigest() == item['checksum']
if str(root) == '/opt/deskport':
    for name, executable in [('display', 'deskport-display'),
                             ('host', 'sunshine/usr/bin/sunshine')]:
        entry = pathlib.Path(f'/usr/share/applications/io.github.keithxc.DeskPort.{name}.desktop')
        metadata = entry.read_text()
        assert f'Exec={root}/usr/libexec/{executable}\n' in metadata, metadata
        assert 'X-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1\n' in metadata, metadata
        assert (root / 'usr/libexec' / executable).is_file(), executable
with tempfile.TemporaryDirectory(prefix='deskport-package-smoke-') as temporary:
    state = pathlib.Path(temporary)
    env = {'PATH': '/usr/bin:/bin', 'HOME': temporary, 'QT_QPA_PLATFORM': 'offscreen',
           'QT_QUICK_BACKEND': 'software', 'SDL_VIDEODRIVER': 'dummy',
           # Verify the self-contained fallback even on a modern build host.
           'DESKPORT_APPIMAGE_RUNTIME': 'auto' if os.environ.get('DESKPORT_TEST_HOST_GRAPHICS') == '1' else 'private'}
    for key in ['XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'XDG_CACHE_HOME', 'XDG_STATE_HOME', 'XDG_RUNTIME_DIR']:
        path = state / key.lower()
        path.mkdir(mode=0o700)
        env[key] = str(path)
    for flag in ['--version', '--help']:
        result = subprocess.run([str(root / 'AppRun'), flag], env=env, cwd=state,
                                capture_output=True, text=True, timeout=30)
        output = result.stdout + result.stderr
        assert result.returncode == 0, output
        assert (version if flag == '--version' else 'Usage:') in output, output
    host = root / 'usr/libexec/deskport-host'
    # Version/API startup alone does not load the client's PipeWire plugin tree.
    # Keep this structural gate alongside the real KWin capture regression.
    if (root / 'usr/shared/lib/libc.so.6').exists():
        for prefix in (root / 'usr', root / 'usr/libexec/sunshine/usr'):
            metadata = json.loads((prefix / 'shared/lib/manifest.json').read_text())
            for name in ('libva.so.2', 'libva-drm.so.2', 'libva-x11.so.2', 'libva-wayland.so.2'):
                assert (prefix / 'lib' / name).is_file(), name
                assert metadata[name]['source_version'] == '2.23.0', metadata[name]
            assert (prefix / 'libexec/deskport-graphics-env.sh').is_file()
            for relative in ('shared/lib/spa-0.2/support/libspa-support.so',
                             'shared/lib/spa-0.2/audioconvert/libspa-audioconvert.so',
                             'shared/lib/spa-0.2/videoconvert/libspa-videoconvert.so',
                             'share/glvnd/egl_vendor.d/50_mesa.json',
                             'share/pipewire/client.conf', 'share/pipewire/client-rt.conf'):
                assert (prefix / relative).is_file(), f'Missing portable PipeWire input: {prefix / relative}'
            for module in ('rt', 'protocol-native', 'client-node', 'client-device',
                           'adapter', 'metadata', 'session-manager'):
                assert (prefix / f'shared/lib/pipewire-0.3/libpipewire-module-{module}.so').is_file(), module
        for launcher in (root / 'AppRun', host):
            text = launcher.read_text()
            assert 'deskport_graphics_env' in text and 'export LIBVA_DRIVERS_PATH=' in text, launcher
            assert 'export __EGL_VENDOR_LIBRARY_DIRS=' in text and '$root/usr/share/glvnd/egl_vendor.d' in text, launcher
            for key, relative in [('SPA_PLUGIN_DIR', 'shared/lib/spa-0.2'),
                                  ('PIPEWIRE_MODULE_DIR', 'shared/lib/pipewire-0.3'),
                                  ('PIPEWIRE_CONFIG_DIR', 'share/pipewire')]:
                assert f'export {key}="$root/usr/{relative}"' in text, launcher
    if host.exists():
        result = subprocess.run([str(host), '--version'], env=env, cwd=state,
                                capture_output=True, text=True, timeout=30, check=True)
        assert 'Sunshine version:' in result.stdout + result.stderr
        assert not (state / '.config/sunshine').exists(), 'Touched standalone Sunshine state'
        assert not (pathlib.Path(env['XDG_CONFIG_HOME']) / 'sunshine').exists(), 'Touched shared Sunshine state'
    if host.exists():
        # A version-only smoke check also accepts an unpatched upstream host.
        # Exercise the management contract required by every new client session.
        config = state / 'host.conf'
        config.write_text(
            'port = 52989\nbind_address = 127.0.0.1\naddress_family = ipv4\n'
            'upnp = disabled\nsystem_tray = disabled\norigin_web_ui_allowed = pc\n'
            'keyboard = disabled\nmouse = disabled\ncontroller = disabled\n'
            'stream_audio = disabled\nencoder = software\n'
            + ''.join(f'{key} = {state / name}\n' for key, name in [
                ('file_apps', 'apps.json'), ('file_state', 'host-state.json'),
                ('credentials_file', 'control.json'), ('pkey', 'key.pem'),
                ('cert', 'cert.pem'), ('log_path', 'host.log')]))
        (state / 'apps.json').write_text('{"apps": []}')
        subprocess.run([str(host), str(config), '--creds', 'package-check', 'isolated-test'],
                       env=env, cwd=state, capture_output=True, timeout=30, check=True)
        context = ssl._create_unverified_context()  # Isolated self-signed loopback server.
        # The smoke server is loopback-only; inherited proxies may be unreachable
        # in its network namespace and must never intercept these requests.
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}),
                                            urllib.request.HTTPSHandler(context=context))
        # Point every proxy variable at a closed port, so a request that does
        # not use the proxy-free opener fails on every host, not only behind one.
        for key in ('http_proxy', 'https_proxy', 'all_proxy'):
            os.environ[key] = os.environ[key.upper()] = 'http://127.0.0.1:9'
        for key in ('no_proxy', 'NO_PROXY'):
            os.environ.pop(key, None)
        authorization = 'Basic ' + base64.b64encode(b'package-check:isolated-test').decode()
        def request_api(payload=None, extra_headers=None):
            headers = {'Authorization': authorization, 'Content-Type': 'application/json'}
            headers.update(extra_headers or {})
            request = urllib.request.Request('https://127.0.0.1:52990/api/deskport/sessions',
                data=None if payload is None else json.dumps(payload).encode(), headers=headers)
            with opener.open(request, timeout=2) as response:
                return json.load(response)
        with (state / 'host-process.log').open('w+') as host_log:
            server = subprocess.Popen([str(host), str(config)], env=env, cwd=state,
                stdout=host_log, stderr=host_log, start_new_session=True)
            try:
                deadline = time.monotonic() + 45
                while True:
                    try:
                        snapshot = request_api()
                        break
                    except urllib.error.HTTPError as error:
                        raise AssertionError(f'Packaged session API returned HTTP {error.code}') from error
                    except (urllib.error.URLError, TimeoutError):
                        if server.poll() is not None or time.monotonic() >= deadline:
                            host_log.seek(0)
                            raise AssertionError(f'Packaged session API unavailable (exit={server.poll()}): ' + host_log.read())
                        time.sleep(0.25)
                private_libc = root / 'usr/libexec/sunshine/usr/shared/lib/libc.so.6'
                if private_libc.exists():
                    maps = pathlib.Path(f'/proc/{server.pid}/maps').read_text()
                    if env['DESKPORT_APPIMAGE_RUNTIME'] == 'private':
                        assert str(private_libc) in maps, 'Host did not use its private glibc'
                    else:
                        assert str(private_libc) not in maps, 'Host did not select the modern driver cohort'
                    expected = root / 'usr/libexec/sunshine/usr/bin/sunshine'
                    assert pathlib.Path(f'/proc/{server.pid}/exe').resolve() == expected
                assert snapshot['status'] is True and snapshot['version'] == 1, snapshot
                assert snapshot['sessions'] == 0 and snapshot['reserved'] is False, snapshot
                assert isinstance(snapshot['snapshot'], str), snapshot
                # Relative asset paths must work inside the relocated host tree.
                page = urllib.request.Request('https://127.0.0.1:52990/',
                                              headers={'Authorization': authorization})
                with opener.open(page, timeout=2) as response:
                    assert b'<html' in response.read().lower(), 'Host web assets unavailable'
                rejected = request_api({'action': 'acquire', 'uuid': 'unpaired',
                    'lease': 'package-check', 'snapshot': snapshot['snapshot']})
                assert rejected['status'] is False and rejected['code'] == 'unauthorized', rejected
                # The browser transport is linked in: its management route answers.
                browser = urllib.request.Request('https://127.0.0.1:52990/api/deskport/browser',
                    data=json.dumps({'action': 'status', 'id': 'package-check-browser'}).encode(),
                    headers={'Authorization': authorization, 'Content-Type': 'application/json'})
                with opener.open(browser, timeout=2) as response:
                    answer = json.load(response)
                assert answer.get('status') is False and answer.get('code') == 'not-found', answer
                released = request_api({'action': 'release', 'lease': 'package-check'})
                assert released['status'] is True and released['sessions'] == 0, released
                for headers in [{'Authorization': ''}, {'Origin': 'https://example.invalid'}]:
                    try:
                        request_api(extra_headers=headers)
                    except urllib.error.HTTPError as error:
                        assert error.code in (400, 401, 403), error
                    else:
                        raise AssertionError('Session API accepted an unauthenticated/browser request')
            finally:
                if server.poll() is None:
                    os.killpg(server.pid, signal.SIGTERM)
                    try:
                        server.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        os.killpg(server.pid, signal.SIGKILL)
                        server.wait()
    with (state / 'gui.log').open('w+') as log:
        child = subprocess.Popen([str(root / 'AppRun'), '--no-host-autostart'], env=env,
                                 cwd=state, stdout=log, stderr=log, start_new_session=True)
        try:
            try:
                code = child.wait(timeout=6)
                log.seek(0)
                raise AssertionError(f'GUI exited before validation ({code}): {log.read()}')
            except subprocess.TimeoutExpired:
                pass
        finally:
            if child.poll() is None:
                os.killpg(child.pid, signal.SIGTERM)
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.wait()
        log.seek(0)
        output = log.read()
        for failure in ['QQmlApplicationEngine failed', 'is not installed', 'Cannot load library']:
            assert failure not in output, output
    assert not (state / '.config/sunshine').exists()
print('PASS: isolated version/help, packaged QML GUI startup, authenticated host session API and state isolation')
