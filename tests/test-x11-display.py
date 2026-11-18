#!/usr/bin/env python3
"""Real isolated Xorg/xf86-video-dummy lease tests; never use the user's DISPLAY.

Run inside the locked devShell. DESKPORT_X11_MODULE_PATH must contain the dummy
and Xorg server module directories. Caller reserves the shared host's compilation/test lock for this headless server.
All state uses TMPDIR; preserve the log by setting DESKPORT_X11_TEST_OUTPUT.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import ctypes
import json
import os
from pathlib import Path
import re
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time

helper = str(Path(sys.argv[1]).resolve())
module_path = os.environ['DESKPORT_X11_MODULE_PATH']
work = Path(os.environ.get('DESKPORT_X11_TEST_OUTPUT') or tempfile.mkdtemp(prefix='deskport-x11-'))
if work.exists() and any(work.iterdir()):
    raise SystemExit('Choose a fresh DESKPORT_X11_TEST_OUTPUT to preserve immutable acceptance evidence')
work.mkdir(parents=True, exist_ok=True)
config = work / 'xorg.conf'
config.write_text('''Section "ServerFlags"
 Option "AutoAddDevices" "false"
 Option "AutoAddGPU" "false"
 Option "DontVTSwitch" "true"
EndSection
Section "Device"
 Identifier "Synthetic"
 Driver "dummy"
 VideoRam 524288
EndSection
Section "Monitor"
 Identifier "Synthetic"
 HorizSync 5-1000
 VertRefresh 5-240
 Modeline "1280x720" 74.25 1280 1390 1430 1650 720 725 730 750
EndSection
Section "Screen"
 Identifier "Synthetic"
 Device "Synthetic"
 Monitor "Synthetic"
 DefaultDepth 24
 SubSection "Display"
  Depth 24
  Modes "1280x720"
  Virtual 16384 8192
 EndSubSection
EndSection
''')
children = []
guardians = set()
suspended = set()
xlog = (work / 'xorg-stdout.log').open('w')
# Let Xorg allocate an unused display; no existing display/socket is claimed.
rfd, wfd = os.pipe()
server = subprocess.Popen([os.environ.get('DESKPORT_TEST_XORG', 'Xorg'), '-displayfd', str(wfd),
    '-config', str(config), '-modulepath', module_path, '-logfile', str(work / 'Xorg.log'),
    '-nolisten', 'tcp', '-noreset', '-novtswitch', '-sharevts'], pass_fds=(wfd,), stdout=xlog, stderr=subprocess.STDOUT)
children.append(server)
os.close(wfd)
try:
    assert select.select([rfd], [], [], 12)[0], (work / 'xorg-stdout.log').read_text()
    display = ':' + os.read(rfd, 100).decode().strip()
    assert re.fullmatch(r':[0-9]+', display), (work / 'xorg-stdout.log').read_text()
    env = os.environ.copy()
    for key in ('WAYLAND_DISPLAY', 'HYPRLAND_INSTANCE_SIGNATURE', 'XAUTHORITY'):
        env.pop(key, None)
    env.update(DISPLAY=display, XDG_SESSION_TYPE='x11', XDG_CURRENT_DESKTOP='DeskPort-Test',
        DESKPORT_X11_OUTPUT='DUMMY1', DESKPORT_DISPLAY_ON_DEMAND='1', DESKPORT_DISPLAY_STATE_DIR=str(work / 'state'))
    subprocess.run(['xrandr', '--output', 'DUMMY0', '--mode', '1280x720', '--primary'], env=env, check=True)
    def query():
        return subprocess.check_output(['xrandr', '--query'], env=env, text=True)
    baseline = query()
    (work / 'baseline.txt').write_text(baseline)
    assert 'DUMMY1 disconnected' in baseline
    def rect(name):
        match = re.search(r'^' + name + r' (?:dis)?connected (?:primary )?(\d+x\d+\+\d+\+\d+)', query(), re.M)
        return match.group(1) if match else None
    def root_size():
        return re.search(r'current (\d+ x \d+)', query()).group(1)
    base_root = root_size()
    xlib_path = subprocess.check_output(['pkg-config', '--variable=libdir', 'x11'], text=True).strip()
    xlib = ctypes.CDLL(str(Path(xlib_path) / 'libX11.so.6'))
    xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]; xlib.XOpenDisplay.restype = ctypes.c_void_p
    for function in ('XDefaultScreen', 'XDisplayWidth', 'XDisplayHeight', 'XDisplayWidthMM', 'XDisplayHeightMM'):
        value = getattr(xlib, function)
        value.argtypes = [ctypes.c_void_p] if function == 'XDefaultScreen' else [ctypes.c_void_p, ctypes.c_int]
        value.restype = ctypes.c_int
    xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
    def screen_dpi():
        connection = xlib.XOpenDisplay(display.encode()); assert connection
        try:
            screen = xlib.XDefaultScreen(connection)
            return (25.4 * xlib.XDisplayWidth(connection, screen) / xlib.XDisplayWidthMM(connection, screen),
                    25.4 * xlib.XDisplayHeight(connection, screen) / xlib.XDisplayHeightMM(connection, screen))
        finally: xlib.XCloseDisplay(connection)
    original_dpi = screen_dpi()

    def restored():
        return rect('DUMMY1') is None and rect('DUMMY0') == '1280x720+0+0' and root_size() == base_root and not list((work / 'state').glob('x11-*.json'))
    def wait_restore():
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if restored(): return
            time.sleep(.05)
        raise AssertionError(query())
    def start(overrides=None):
        e = env.copy(); e.update(overrides or {})
        p = subprocess.Popen([helper, '1280', '720'], env=e, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        children.append(p)
        value = read(p)
        try:
            for child in Path(f'/proc/{p.pid}/task/{p.pid}/children').read_text().split():
                guardians.add(int(child))
        except FileNotFoundError:
            pass
        return p, value
    def read(p):
        assert select.select([p.stdout], [], [], 8)[0], ('helper timeout', p.poll())
        line = p.stdout.readline()
        assert line, ('helper exited', p.poll(), p.stderr.read().decode())
        value = json.loads(line)
        with (work / 'replies.jsonl').open('a') as log: log.write(json.dumps(value) + '\n')
        return value
    seq = 0
    def request(p, w=1280, h=720, scale=1, **extra):
        global seq
        seq += 1
        p.stdin.write((json.dumps(dict(seq=seq, width=w, height=h, scale=scale, **extra)) + '\n').encode()); p.stdin.flush()
        value = read(p); assert value['seq'] == seq
        return value
    def activate(p, w=1280, h=720):
        value = request(p, w, h)
        assert 'error' not in value and value['active'] and value['scale'] == 1, value
        assert rect('DUMMY1') == f'{w}x{h}+1280+0', query()
        assert rect('DUMMY0') == '1280x720+0+0'
        assert all(abs(a-b) < 1 for a, b in zip(screen_dpi(), original_dpi)), (screen_dpi(), original_dpi)
        assert re.fullmatch('DeskPort-[a-f0-9-]{36}', value['leaseToken'])
        return value
    capture_count = 0
    def capture(identity, missing=False):
        global capture_count
        host = os.environ.get('DESKPORT_TEST_HOST')
        if not host: return
        capture_count += 1
        case = work / f'capture-{capture_count}'; case.mkdir()
        descriptor = case / 'virtual-display.json'
        descriptor.write_text(json.dumps(dict(output=identity['outputName'], width=identity['width'],
            height=identity['height'], scale=identity['scale'], leaseToken=identity['leaseToken'])))
        config = case / 'sunshine.conf'
        config.write_text(f'''capture = x11
output_name = {identity['outputName']}
encoder = software
keyboard = disabled
mouse = disabled
controller = disabled
stream_audio = disabled
upnp = disabled
system_tray = disabled
bind_address = 127.0.0.1
port = 58989
min_log_level = 1
file_state = {case}/state.json
credentials_file = {case}/control.json
pkey = {case}/key.pem
cert = {case}/cert.pem
file_apps = {case}/apps.json
log_path = {case}/sunshine.log
''')
        (case / 'apps.json').write_text('{"apps": []}')
        host_env = dict(env, DESKPORT_VIRTUAL_DISPLAY=str(descriptor),
            DBUS_SYSTEM_BUS_ADDRESS='unix:path=' + str(case / 'no-system-bus'))
        host_env.pop('DESKPORT_ON_DEMAND_DISPLAY', None)
        with (case / 'probe.log').open('w+') as log:
            process = subprocess.Popen(['unshare', '-Urn', '--', host, str(config)], env=host_env, cwd=case,
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
                except subprocess.TimeoutExpired: process.kill(); process.wait(timeout=5)
        if missing:
            assert 'Found H.264 encoder:' not in text and 'refusing physical display fallback' in text, text[-10000:]
            print('PASS: unavailable or overlapping owned connector refuses physical/root capture fallback', flush=True)
        else:
            assert 'Found H.264 encoder:' in text, text[-10000:]
            assert f"DeskPort owned X11 capture: DUMMY1 {identity['width']}x{identity['height']}" in text, text[-10000:]
            print(f"PASS: real X11/software H.264 encoder probe {identity['width']}x{identity['height']}@1", flush=True)
    p, ready = start(); assert ready['ready'] and not ready['active'] and restored(), ready
    identity = activate(p)
    capture(identity)
    duplicate, reply = start(); assert 'already leased' in reply['error'], reply
    duplicate.wait(timeout=5)
    for w, h in ((1668, 2388), (1920, 1080), (640, 360), (2560, 1440), (1280, 720)):
        identity = activate(p, w, h)
        if (w, h) == (1668, 2388): capture(identity)
        before = query()
        assert 'error' in request(p, w, h, 2) and query() == before
    assert 'error' in request(p, displayPolicy=0)
    assert 'error' in request(p, w=641)
    assert not request(p, session=False)['active']; wait_restore()
    capture(identity, missing=True)
    identity = activate(p, 1920, 1080)
    capture(identity)
    p.stdin.close(); assert p.wait(timeout=5) == 0; wait_restore()
    print('PASS: independent creation, five resizes, observed 1x, explicit 2x/policy rejection, release and recreation', flush=True)
    p, ready = start(); activate(p)
    p.kill(); p.wait(timeout=5); wait_restore()
    print('PASS: helper SIGKILL triggers independent guardian removal and framebuffer restoration', flush=True)
    p, ready = start(); activate(p)
    guard_pids = Path(f'/proc/{p.pid}/task/{p.pid}/children').read_text().split()
    assert len(guard_pids) == 1, guard_pids
    os.kill(int(guard_pids[0]), signal.SIGKILL)
    assert p.wait(timeout=5) != 0
    assert list((work / 'state').glob('x11-*.json'))
    p, ready = start(); assert ready['ready'], ready
    wait_restore(); activate(p, 1668, 2388)
    p.stdin.close(); assert p.wait(timeout=5) == 0; wait_restore()
    print('PASS: guardian death retains journal; next start recovers before admission', flush=True)
    p, ready = start(); activate(p)
    subprocess.run(['xrandr', '--output', 'DUMMY0', '--pos', '0x4'], env=env, check=True)
    reply = request(p, session=False)
    assert 'error' not in reply and reply['layoutWarning'] and not reply['active'], reply
    p.stdin.close(); assert p.wait(timeout=5) == 0
    assert rect('DUMMY1') is None and rect('DUMMY0') == '1280x720+0+4'
    assert list((work / 'state').glob('x11-*.pending-*.json'))
    # Pending local recovery must not block another verified independent lease.
    p, ready = start(); assert ready['ready'] and ready['layoutWarning'], ready
    reply = request(p, 1280, 720)
    assert 'error' not in reply and reply['layoutWarning'] and rect('DUMMY1') == '1280x720+1280+0', reply
    reply = request(p, 1920, 1080)
    assert 'error' not in reply and rect('DUMMY1') == '1920x1080+1280+0', reply
    assert rect('DUMMY0') == '1280x720+0+4'
    assert all(abs(a-b) < 1 for a, b in zip(screen_dpi(), original_dpi))
    reply = request(p, session=False); assert 'error' not in reply and not reply['active'], reply
    p.stdin.close(); assert p.wait(timeout=5) == 0
    subprocess.run(['xrandr', '--output', 'DUMMY0', '--pos', '0x0', '--fb', base_root.replace(' ', '')], env=env, check=True)
    p, ready = start(); assert ready['ready'] and not ready.get('layoutWarning'), ready
    wait_restore(); p.stdin.close(); assert p.wait(timeout=5) == 0
    print('PASS: local layout edits survive cleanup; pending recovery admits a new lease and completes only after matching baseline restoration', flush=True)
    p, ready = start(); activate(p)
    subprocess.run(['xrandr', '--dpi', '120'], env=env, check=True)
    reply = request(p, session=False); assert not reply.get('error') and reply.get('layoutWarning'), (reply, screen_dpi())
    p.stdin.close(); assert p.wait(timeout=5) == 0
    assert all(abs(value - 120) < 1 for value in screen_dpi()), screen_dpi()
    p, ready = start(); assert ready['layoutWarning'], ready
    reply = request(p, 1920, 1080); assert not reply.get('error') and reply['active'], reply
    assert all(abs(value - 120) < 1 for value in screen_dpi()), screen_dpi()
    assert not request(p, session=False).get('error')
    p.stdin.close(); assert p.wait(timeout=5) == 0
    subprocess.run(['xrandr', '--fb', base_root.replace(' ', ''), '--dpi', str(original_dpi[0])], env=env, check=True)
    p, ready = start(); assert ready['ready'] and not ready.get('layoutWarning'), ready
    wait_restore(); p.stdin.close(); assert p.wait(timeout=5) == 0
    print('PASS: a concurrent global-DPI edit is preserved through cleanup and a later independent lease', flush=True)
    p, ready = start(); identity = activate(p)
    guard_pid = int(Path(f'/proc/{p.pid}/task/{p.pid}/children').read_text().split()[0])
    os.kill(guard_pid, signal.SIGSTOP); suspended.add(guard_pid)
    try:
        owned_fb = root_size().replace(' ', '')
        for label, command, reset in (
            ('transform', ['--transform', '2,0,0,0,2,0,0,0,1'], ['--transform', 'none']),
            ('panning', ['--panning', '1284x720+1280+0'], ['--panning', '0x0'])):
            change = subprocess.run(['xrandr', '--output', 'DUMMY1', *command], env=env, text=True, capture_output=True)
            if change.returncode == 0:
                if os.environ.get('DESKPORT_TEST_HOST'):
                    capture(identity, missing=True)
                    print(f'PASS: RandR {label} is refused as a 1x owned capture', flush=True)
                else:
                    print(f'FIXTURE: RandR {label} accepted by driver; capture check requires the packaged host', flush=True)
                subprocess.run(['xrandr', '--output', 'DUMMY1', *reset], env=env, check=True)
            else:
                print(f'CAPABILITY: dummy driver rejected {label}; not a UI-scale backend', flush=True)
                if label == 'transform':
                    subprocess.run(['xrandr', '--output', 'DUMMY1', *reset], env=env, check=True)
            subprocess.run(['xrandr', '--fb', owned_fb], env=env, check=True)
        subprocess.run(['xrandr', '--addmode', 'DUMMY2', '1280x720'], env=env, check=True)
        subprocess.run(['xrandr', '--output', 'DUMMY2', '--mode', '1280x720', '--pos', '1280x0'], env=env, check=True)
        assert rect('DUMMY1') == rect('DUMMY2') == '1280x720+1280+0'
        capture(identity, missing=True)  # The owned mode still exists; only its independence was lost.
    finally:
        os.kill(guard_pid, signal.SIGCONT); suspended.discard(guard_pid)
    assert p.wait(timeout=5) != 0
    assert rect('DUMMY1') is None and rect('DUMMY2') == '1280x720+1280+0'
    subprocess.run(['xrandr', '--output', 'DUMMY2', '--off', '--fb', base_root.replace(' ', '')], env=env, check=True)
    subprocess.run(['xrandr', '--delmode', 'DUMMY2', '1280x720'], env=env, check=True)
    p, ready = start(); assert ready['ready'], ready
    wait_restore(); p.stdin.close(); assert p.wait(timeout=5) == 0
    print('PASS: overlapping scanout is refused, owned output is removed, and the other owner is preserved', flush=True)
    for override in ({'DISPLAY': ''}, {'WAYLAND_DISPLAY': 'deskport-test-unavailable'}, {'XDG_SESSION_TYPE': 'wayland'}):
        rejected, value = start(override); assert 'error' in value, value
        assert rejected.wait(timeout=5) != 0 and restored()
    for name in ('HDMI-1', 'DUMMY0', 'DUMMY16', ''):
        rejected, value = start({'DESKPORT_X11_OUTPUT': name}); assert 'error' in value, value
        assert rejected.wait(timeout=5) != 0
        assert restored()
    # An already-active dummy connector belongs to somebody else and is refused.
    rejected, value = start({'DESKPORT_X11_OUTPUT': 'DUMMY0'}); assert 'error' in value
    assert rejected.wait(timeout=5) != 0
    subprocess.run(['xrandr', '--addmode', 'DUMMY1', '1280x720'], env=env, check=True)
    subprocess.run(['xrandr', '--output', 'DUMMY1', '--mode', '1280x720', '--right-of', 'DUMMY0'], env=env, check=True)
    occupied = query()
    rejected, value = start(); assert 'active' in value['error'], value
    assert rejected.wait(timeout=5) != 0 and query() == occupied
    subprocess.run(['xrandr', '--output', 'DUMMY1', '--off', '--fb', base_root.replace(' ', '')], env=env, check=True)
    wait_restore()
    pan = subprocess.run(['xrandr', '--output', 'DUMMY0', '--panning', '1284x720+0+0'], env=env, text=True, capture_output=True)
    if pan.returncode == 0:
        before = query()
        rejected, value = start(); assert 'error' in value and 'existing outputs' in value['error'], value
        assert rejected.wait(timeout=5) != 0 and query() == before
        subprocess.run(['xrandr', '--output', 'DUMMY0', '--panning', '0x0', '--fb', base_root.replace(' ', '')], env=env, check=True)
        wait_restore()
        print('PASS: existing panning output refuses adaptive admission without mutation or physical fallback', flush=True)
    print('PASS: physical, unselected, invalid and reserved connectors refused without mutation', flush=True)
    (work / 'restored.txt').write_text(query())
finally:
    for pid in suspended:
        try: os.kill(pid, signal.SIGCONT)
        except ProcessLookupError: pass
    os.close(rfd)
    for child in reversed(children):
        if child.poll() is None:
            if child is server: child.terminate()
            elif child.stdin and not child.stdin.closed: child.stdin.close()
            else: child.terminate()
            try: child.wait(timeout=5)
            except subprocess.TimeoutExpired: child.kill(); child.wait(timeout=5)
    assert all(child.poll() is not None for child in children)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline and any(Path(f'/proc/{pid}').exists() for pid in guardians):
        time.sleep(.05)
    assert not [pid for pid in guardians if Path(f'/proc/{pid}').exists()], guardians
    if 'display' in locals() and re.fullmatch(r':[0-9]+', display):
        number = display[1:]
        assert not Path(f'/tmp/.X11-unix/X{number}').exists()
        assert not Path(f'/tmp/.X{number}-lock').exists()
    xlog.close()
    print(f'TEARDOWN: owned Xorg and helpers stopped; evidence {work}', flush=True)
