#!/usr/bin/env python3
"""Real Hyprland lifecycle inside virtual KWin; no personal desktop access.

Needs Hyprland, hyprctl, kwin_wayland, dbus-run-session and the locked Qt devShell.
Uses a private D-Bus bus and XDG directories. DRM seat access is disabled for
this nested test. Set DESKPORT_TEST_HOST to include actual WLR/software encoder
probing and refusal to capture a lost output. It never injects live input.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import argparse
from contextlib import contextmanager
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
import uuid

parser = argparse.ArgumentParser()
parser.add_argument('--helper')
parser.add_argument('--lua', action='store_true', help='Use the current Lua compositor configuration')
parser.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]


def until(check, message, timeout=15):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = check()
        if value: return value
        time.sleep(.05)
    raise AssertionError(message)


def receive(process):
    assert select.select([process.stdout], [], [], 8)[0], 'helper response timeout'
    value = process.stdout.readline()
    assert value, f'helper exited {process.poll()}'
    return json.loads(value)


def request(process, seq, width=1280, height=720, scale=1, **extra):
    process.stdin.write(json.dumps(dict(seq=seq, width=width, height=height, scale=scale, **dict({'displayPolicy': 2}, **extra))) + '\n')
    process.stdin.flush()
    response = receive(process)
    assert response.get('seq') == seq and 'error' not in response, response
    return response


@contextmanager
def short_workspace(target):
    link = Path('/tmp') / ('dphy-' + uuid.uuid4().hex[:8])
    link.symlink_to(target, target_is_directory=True)
    try:
        yield link
    finally:
        link.unlink()


if not args.inside:
    # Hyprland's instance signatures are long; UNIX sockets need a short root.
    test_root = Path(subprocess.check_output(['bash', str(root / 'scripts/build-paths.sh'), 'checkout'], text=True).strip()) / 'tests'
    test_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='hyprland-live-', dir=test_root) as tmp, short_workspace(tmp) as work:
        helper = str(Path(args.helper).resolve()) if args.helper else str(work / 'helper')
        if not args.helper:
            subprocess.run([sys.executable, str(root / 'tests/test-hyprland-display.py'), '--build-only', helper], check=True)
        for name in ['runtime', 'config', 'data', 'cache']:
            (work / name).mkdir(mode=0o700)
        env = dict(os.environ, XDG_RUNTIME_DIR=str(work / 'runtime'), XDG_CONFIG_HOME=str(work / 'config'),
                   XDG_DATA_HOME=str(work / 'data'), XDG_CACHE_HOME=str(work / 'cache'),
                   DESKPORT_TEST_WORK=str(work), XDG_CURRENT_DESKTOP='Hyprland',
                   XDG_SESSION_TYPE='wayland', XDG_SESSION_DESKTOP='Hyprland',
                   AQ_DRM_DEVICES='/dev/null', LIBSEAT_BACKEND='seatd', SEATD_SOCK=str(work / 'no-seat'),
                   HYPRLAND_NO_RT='1', HYPRLAND_NO_SD_NOTIFY='1', HYPRLAND_NO_CRASHREPORT='1',
                   QT_QPA_PLATFORM='offscreen', DESKPORT_DISPLAY_ON_DEMAND='1')
        for key in ['DISPLAY', 'WAYLAND_DISPLAY', 'HYPRLAND_INSTANCE_SIGNATURE', 'DBUS_SESSION_BUS_ADDRESS']:
            env.pop(key, None)
        bus_config = work / 'session-bus.conf'
        bus_config.write_text(f'<busconfig><type>session</type><listen>unix:path={work}/runtime/session-bus</listen><auth>EXTERNAL</auth><policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>')
        result = subprocess.run(['dbus-run-session', '--config-file=' + str(bus_config), '--', sys.executable,
                                 str(Path(__file__).resolve()), '--inside', '--helper', helper] + (['--lua'] if args.lua else []), env=env)
        if result.returncode:
            for name in ['kwin.log', 'hyprland.log', 'helper.log']:
                file = work / name
                if file.exists(): print(f'{name}:\n' + file.read_text(errors='replace')[-12000:], file=sys.stderr)
        raise SystemExit(result.returncode)

work = Path(os.environ['DESKPORT_TEST_WORK'])
assert str(work / 'runtime') == os.environ['XDG_RUNTIME_DIR']
env = dict(os.environ)
children = []
guardians = {}
forced_guard_cleanup = False


def record_guardians():
    for lock in (work / 'runtime/deskport-hyprland').glob('*/lease.lock'):
        try:
            pid = int(lock.read_text().splitlines()[0])
            fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
            guardians[pid] = fields[19]
        except (OSError, ValueError, IndexError):
            pass


def guardian_alive(pid, started):
    try:
        fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
        return fields[19] == started and fields[0] != 'Z'
    except (OSError, IndexError):
        return False


try:
    kwin_log = open(work / 'kwin.log', 'w')
    kwin_binary = shutil.which('kwin_wayland', path=os.pathsep.join(p for p in env['PATH'].split(os.pathsep) if '/wrappers/' not in p))
    assert kwin_binary, 'kwin_wayland is required as an isolated parent compositor'
    kwin = subprocess.Popen([kwin_binary, '--virtual', '--output-count', '1', '--socket', 'deskport-test-parent',
                             '--width', '1280', '--height', '720', '--no-lockscreen', '--no-global-shortcuts', '--no-kactivities'],
                            env=env, stdout=kwin_log, stderr=subprocess.STDOUT)
    children.append(kwin)
    until(lambda: (work / 'runtime/deskport-test-parent').exists(), 'isolated KWin did not start')
    env['WAYLAND_DISPLAY'] = 'deskport-test-parent'
    config = work / ('config/hyprland.lua' if args.lua else 'config/hyprland.conf')
    config.write_text('hl.monitor({ output = "", mode = "1280x720@60", position = "auto", scale = 1 })\n' if args.lua else
                      'monitor = ,1280x720@60,auto,1\nmisc {\n disable_hyprland_logo = true\n disable_splash_rendering = true\n}\n')
    hypr_log = open(work / 'hyprland.log', 'w')
    hyprland = subprocess.Popen(['Hyprland', '--config', str(config)], env=env, stdout=hypr_log, stderr=subprocess.STDOUT)
    children.append(hyprland)
    socket = until(lambda: next((work / 'runtime/hypr').glob('*/.socket.sock'), None), 'isolated Hyprland IPC did not start')
    env['HYPRLAND_INSTANCE_SIGNATURE'] = socket.parent.name
    wayland = until(lambda: next((p for p in (work / 'runtime').glob('wayland-*') if not p.name.endswith('.lock')), None),
                    'isolated Hyprland Wayland socket did not start')
    env['WAYLAND_DISPLAY'] = wayland.name

    def monitors():
        return json.loads(subprocess.check_output(['hyprctl', '-j', 'monitors', 'all'], env=env, text=True))

    def ctl(*arguments):
        result = subprocess.check_output(['hyprctl', *arguments], env=env, text=True).strip()
        assert result == 'ok', (arguments, result)

    names = {m['name'] for m in monitors()}
    ctl('output', 'create', 'headless')
    original_headless = until(lambda: next((m['name'] for m in monitors() if m['name'] not in names), None), 'second original output missing')
    # Give originals stable private rules. A software output removed/recreated
    # later should return to its rule, rather than being appended after hotplug
    # outputs by Hyprland's automatic placement algorithm.
    for m in monitors():
        position = '1280x0' if m['name'] == original_headless else '0x0'
        if args.lua:
            ctl('eval', f'hl.monitor({{ output = "{m["name"]}", mode = "1280x720@60", position = "{position}", scale = 1 }})')
        else:
            ctl('keyword', 'monitor', f'{m["name"]},1280x720@60,{position},1')
    until(lambda: all(m['x'] == (1280 if m['name'] == original_headless else 0) and m['y'] == 0 for m in monitors()), 'original private layout did not settle')
    baseline = until(lambda: monitors(), 'isolated Hyprland has no initial output')
    original = {m['name']: (m['width'], m['height'], m['scale'], m['x'], m['y'], m['transform'], m['mirrorOf'], m['disabled']) for m in baseline}

    def check_original():
        current = {m['name']: (m['width'], m['height'], m['scale'], m['x'], m['y'], m['transform'], m['mirrorOf'], m['disabled'])
                   for m in monitors() if m['name'] in original}
        assert current == original, (current, original)

    def clean():
        return not any(m['name'].startswith('DeskPort-') for m in monitors())

    helper_log = open(work / 'helper.log', 'w')
    capture_count = 0
    host = os.environ.get('DESKPORT_TEST_HOST')
    host_namespace = []
    if host and shutil.which('unshare'):
        check = subprocess.run(['unshare', '--user', '--map-root-user', '--net', 'true'],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if check.returncode == 0: host_namespace = ['unshare', '--user', '--map-root-user', '--net']

    def capture(identity, missing=False):
        global capture_count
        if not host: return
        case = work / f'capture-{capture_count}'
        case.mkdir()
        capture_count += 1
        descriptor = case / 'virtual-display.json'
        descriptor.write_text(json.dumps(dict(output=identity['outputName'], width=identity['width'],
                                              height=identity['height'], scale=identity['scale'])))
        config = case / 'sunshine.conf'
        config.write_text(f'''capture = wlr
output_name = {identity['outputName']}
encoder = software
keyboard = disabled
mouse = disabled
controller = disabled
stream_audio = disabled
upnp = disabled
system_tray = disabled
bind_address = 127.0.0.1
port = {58989 + capture_count * 100}
min_log_level = 1
file_state = {case}/state.json
credentials_file = {case}/control.json
pkey = {case}/key.pem
cert = {case}/cert.pem
file_apps = {case}/apps.json
log_path = {case}/sunshine.log
''')
        (case / 'apps.json').write_text('{"apps": []}')
        host_env = dict(env, DBUS_SYSTEM_BUS_ADDRESS='unix:path=' + str(case / 'no-system-bus'),
                        DESKPORT_VIRTUAL_DISPLAY=str(descriptor))
        host_env.pop('DESKPORT_ON_DEMAND_DISPLAY', None)
        with open(case / 'probe.log', 'w+') as log:
            process = subprocess.Popen(host_namespace + [host, str(config)], env=host_env, cwd=case,
                                       stdout=log, stderr=subprocess.STDOUT)
            children.append(process)
            deadline = time.monotonic() + 25
            text = ''
            while time.monotonic() < deadline:
                log.seek(0); text = log.read()
                if 'Found H.264 encoder:' in text or 'refusing physical display fallback' in text or process.poll() is not None: break
                time.sleep(.1)
            if process.poll() is None:
                process.terminate()
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired: process.kill(); process.wait()
        if missing:
            assert 'Found H.264 encoder:' not in text and 'refusing physical display fallback' in text, text[-12000:]
            print('PASS: removed Hyprland output refuses physical capture fallback', flush=True)
        else:
            assert 'Found H.264 encoder:' in text, text[-12000:]
            assert identity['outputName'] in text and f"Resolution: {identity['width']}x{identity['height']}" in text, text[-12000:]
            print(f"PASS: real WLR/software encoder probe {identity['width']}x{identity['height']}@{identity['scale']}", flush=True)

    def start():
        process = subprocess.Popen([args.helper, '1280', '720'], env=env, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=helper_log, text=True, start_new_session=True)
        children.append(process)
        assert receive(process)['ready']
        record_guardians()
        assert clean()
        return process

    helper = start()
    for seq, (width, height, scale) in enumerate([(1280, 720, 1), (1668, 2388, 2), (1920, 1080, 1)], 1):
        response = request(helper, seq, width, height, scale)
        output = next(m for m in monitors() if m['name'] == response['outputName'])
        assert (output['width'], output['height'], output['scale']) == (width, height, scale), output
        check_original()
        capture(response)
    assert not request(helper, 4, session=False)['active']
    until(clean, 'disconnect did not remove the owned output')
    capture(response, missing=True)
    request(helper, 5)
    helper.stdin.close()
    assert helper.wait(timeout=8) == 0
    until(clean, 'EOF did not remove the owned output')
    # Apply each topology policy against real outputs; mode/scale changes keep
    # the initial recovery snapshot. A later hotplug is never a policy target.
    for policy in [0, 1, 2]:
        helper = start()
        response = request(helper, 10, displayPolicy=policy)
        owned = response['outputName']
        current = monitors()
        target = next(m for m in current if m['name'] == owned)
        for m in current:
            if m['name'] not in original: continue
            if policy == 0: assert str(m['mirrorOf']) in [str(target['id']), owned], m
            if policy == 1: assert m['disabled'], m
        capture(response)
        # The synthetic added screen lives only in this nested compositor.
        ctl('output', 'create', 'headless', 'DeskPortTest-Hotplug')
        added = until(lambda: next((m for m in monitors() if m['name'] == 'DeskPortTest-Hotplug'), None), 'hotplug output missing')
        request(helper, 11, 1668, 2388, 2, displayPolicy=policy)
        current = next(m for m in monitors() if m['name'] == added['name'])
        assert not current['disabled'] and current['mirrorOf'] == added['mirrorOf'], current
        if policy == 0:
            # Guardian death must restore through the durable runtime journal.
            lock = next((work / 'runtime/deskport-hyprland').glob('*/lease.lock'))
            guard = int(lock.read_text().splitlines()[0]); os.kill(guard, signal.SIGKILL)
            assert 'error' in receive(helper)
            assert helper.wait(timeout=20) == 1
        elif policy == 1:
            helper.kill(); helper.wait()
        else:
            ctl('output', 'remove', original_headless)
            until(lambda: not any(m['name'] == original_headless for m in monitors()), 'original output did not detach')
            helper.stdin.close(); assert helper.wait(timeout=15) == 0
        until(clean, 'policy/crash cleanup did not remove owned output')
        if policy == 2:
            ctl('output', 'create', 'headless', original_headless)
            until(lambda: any(m['name'] == original_headless for m in monitors()), 'original output did not return')
        check_original()
        ctl('output', 'remove', added['name'])
        until(lambda: not any(m['name'] == added['name'] for m in monitors()), 'hotplug output did not detach')
        print(f'PASS: policy {policy}, resize/2x, hotplug exclusion and restoration', flush=True)
    # Retain real persistent workspace IDs as well as tolerating Hyprland's
    # automatic empty-workspace destruction/reuse in the earlier cases.
    spaces = json.loads(subprocess.check_output(['hyprctl', '-j', 'workspaces'], env=env, text=True))
    saved_spaces = {w['id']: w['monitor'] for w in spaces if w['monitor'] in original}
    for wid in saved_spaces:
        if args.lua: ctl('eval', f'hl.workspace_rule({{ workspace = "{wid}", persistent = true }})')
        else: ctl('keyword', 'workspace', f'{wid},persistent:true')
    persistent_ids = set(saved_spaces)
    until(lambda: {w['id'] for w in json.loads(subprocess.check_output(['hyprctl', '-j', 'workspaces'], env=env, text=True)) if w.get('ispersistent') and w['id'] in persistent_ids} == persistent_ids, 'private persistent workspace rules did not apply')
    # Persistent-rule activation can recreate an empty workspace on the default
    # output. Snapshot the settled fixture state that the lease actually sees.
    spaces = json.loads(subprocess.check_output(['hyprctl', '-j', 'workspaces'], env=env, text=True))
    saved_spaces = {w['id']: w['monitor'] for w in spaces if w.get('ispersistent') and w['monitor'] in original}
    assert saved_spaces, 'no persistent workspace survived fixture setup'
    def check_spaces():
        spaces = json.loads(subprocess.check_output(['hyprctl', '-j', 'workspaces'], env=env, text=True))
        assert {w['id']: w['monitor'] for w in spaces if w['id'] in saved_spaces} == saved_spaces
    helper = start(); request(helper, 7, displayPolicy=0)
    os.killpg(helper.pid, signal.SIGTERM); helper.wait(timeout=15)
    until(clean, 'process-group SIGTERM did not remove the owned output')
    check_original(); check_spaces()
    # Preserve a pre-existing mirror graph as well as independent displays.
    # Switching mirror sources must not leave an old source's replication list.
    source = next(m for m in monitors() if m['name'] in original and m['name'] != original_headless)
    if args.lua:
        ctl('eval', f'hl.monitor({{ output = "{original_headless}", mode = "1280x720@60", position = "0x0", scale = 1, transform = 0, mirror = "{source["name"]}", disabled = false }})')
    else:
        ctl('keyword', 'monitor', f'{original_headless},1280x720@60,0x0,1,transform,0,mirror,{source["name"]}')
    until(lambda: next(m for m in monitors() if m['name'] == original_headless)['mirrorOf'] == str(source['id']), 'pre-existing mirror did not activate')
    original = {m['name']: (m['width'], m['height'], m['scale'], m['x'], m['y'], m['transform'], m['mirrorOf'], m['disabled']) for m in monitors()}
    for policy in [0, 1]:
        helper = start()
        response = request(helper, 30, 1668, 2388, 2, displayPolicy=policy)
        assert not response.get('layoutWarning'), response
        capture(response)
        helper.stdin.close(); assert helper.wait(timeout=20) == 0
        until(clean, 'pre-existing mirror lease did not clean up')
        check_original()
        print(f'PASS: policy {policy} restores the original mirror graph after removing the owned source', flush=True)
    check_original()
    print('Real isolated Hyprland passed: 3 pixel/scale modes, 3 policies, hotplug exclusion, original/mirrored layouts, release/recreate, EOF, owner/guardian SIGKILL and group SIGTERM cleanup')
    if host: print('Host probes used a private network namespace' if host_namespace else 'Host probes bound loopback only; user network namespaces were unavailable')
except BaseException:
    # Preserve failure state before the private compositor is torn down.
    try:
        print('Failure monitors: ' + json.dumps(monitors()), file=sys.stderr)
        print('Failure workspaces: ' + subprocess.check_output(['hyprctl', '-j', 'workspaces'], env=env, text=True), file=sys.stderr)
        for journal in (work / 'runtime/deskport-hyprland').glob('*/recovery.json'):
            print('Failure recovery journal: ' + journal.read_text(), file=sys.stderr)
        for child in children:
            if child.poll() is not None and child.stdout is not None:
                print('Exited helper output: ' + child.stdout.read(), file=sys.stderr)
    except (OSError, subprocess.SubprocessError):
        pass
    raise
finally:
    record_guardians()
    for child in reversed(children):
        if child in [locals().get('hyprland'), locals().get('kwin')]:
            # Let EOF recovery finish before stopping its private compositor.
            # PID plus kernel start time prevents touching a reused PID.
            record_guardians()
            for pid, started in guardians.items():
                end = time.monotonic() + 12
                while guardian_alive(pid, started) and time.monotonic() < end:
                    time.sleep(.05)
                if guardian_alive(pid, started):
                    forced_guard_cleanup = True
                    os.kill(pid, signal.SIGKILL)
                    until(lambda: not guardian_alive(pid, started), 'owned guardian did not stop', timeout=3)
                    print('Forced cleanup of a failed guardian in the private compositor', file=sys.stderr)
        if child.poll() is None:
            child.terminate()
            try: child.wait(timeout=5)
            except subprocess.TimeoutExpired: child.kill(); child.wait()

    assert all(child.poll() is not None for child in children), 'owned child process survived teardown'
    assert not any(guardian_alive(pid, started) for pid, started in guardians.items()), 'owned guardian survived teardown'
    assert not forced_guard_cleanup, 'guardian required forced teardown'
