#!/usr/bin/env python3
"""Exercise the production Hyprland adapter with an isolated fake IPC endpoint.

Run inside the locked devShell. The tests never connect to the user's compositor.
An explicit --helper may supply an already compiled deskport-display executable.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import argparse
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

parser = argparse.ArgumentParser()
parser.add_argument('--helper')
parser.add_argument('--build-only', metavar='OUTPUT')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]

MOCK = r'''import fcntl, json, os, re, sys, time
from pathlib import Path
p = Path(os.environ['DESKPORT_TEST_HYPR_STATE'])
def matches(m, selector):
 if m['name'] == selector: return True
 if not selector.startswith('desc:'): return False
 full = '{} {} {} ({})'.format(m.get('make', ''), m.get('model', ''), m.get('serial', ''), m['name']).replace(',', '')
 return full.startswith(selector[5:]) or m.get('description', '').startswith(selector[5:])
if sys.argv[1:4] == ['output', 'create', 'headless'] and os.environ.get('DESKPORT_TEST_HYPR_PRE_CREATE_DELAY'):
 p.with_suffix('.creating').touch()
 time.sleep(.8)
with open(str(p) + '.lock', 'w') as lock:
 fcntl.flock(lock, fcntl.LOCK_EX)
 state = json.loads(p.read_text())
 args = sys.argv[1:]
 state.setdefault('commands', []).append(args)
 result = 'ok'
 if args == ['-j', 'monitors', 'all']:
  result = json.dumps(state['monitors'])
 elif args == ['-j', 'workspaces']:
  result = json.dumps(state.get('workspaces', []))
 elif args[0] == 'eval' and 'hl.dsp.workspace.move' in args[1]:
  if os.environ.get('DESKPORT_TEST_HYPR_LEGACY'):
   result = 'eval is only supported with the lua config manager'
  else:
   wid = int(re.search(r'workspace = "(\d+)"', args[1])[1])
   monitor = re.search(r'monitor = "([^"]+)"', args[1])[1]
   for w in state.get('workspaces', []):
    if w['id'] == wid: w['monitor'] = monitor
 elif args[:2] == ['dispatch', 'moveworkspacetomonitor']:
  if not os.environ.get('DESKPORT_TEST_HYPR_LEGACY'):
   result = 'dispatch in lua requires hl.dispatch closure syntax'
  else:
   wid, monitor = args[2].split()
   for w in state.get('workspaces', []):
    if w['id'] == int(wid): w['monitor'] = monitor
 elif args[:3] == ['output', 'create', 'headless']:
  state['monitors'].append(dict(name=args[3], currentFormat='XRGB8888', colorManagementPreset='srgb', vrr=False, id=1, width=1280, height=720, refreshRate=60, x=1920, y=0, scale=1, transform=0, disabled=False, mirrorOf='none'))
 elif args[:2] == ['output', 'remove']:
  state['monitors'] = [m for m in state['monitors'] if m['name'] != args[2]]
 elif args[0] in ['eval', 'keyword']:
  if args[0] == 'eval' and os.environ.get('DESKPORT_TEST_HYPR_LEGACY'):
   result = 'eval is only supported with the lua config manager'
  else:
   if args[0] == 'eval':
    name = re.search(r'output = "([^"]+)"', args[1])[1]
    w, h, refresh = map(float, re.search(r'mode = "(\d+)x(\d+)@([0-9.]+)"', args[1]).groups())
    scale = float(re.search(r'scale = ([0-9.]+)', args[1])[1])
    pos = re.search(r'position = "([^"]+)"', args[1])[1]
    transform = int(re.search(r'transform = (\d+)', args[1])[1])
    mirror = re.search(r'mirror = "([^"]*)"', args[1])[1] or 'none'
    disabled = 'disabled = true' in args[1]
   else:
    fields = args[2].split(','); name = fields[0]; disabled = fields[1] == 'disable'
    if not disabled:
     mode, pos, scale, _, transform = fields[1:6]
     mirror = fields[7] if len(fields) > 7 and fields[6] == 'mirror' else 'none'
     w, h = map(int, mode.split('@')[0].split('x')); refresh = float(mode.split('@')[1]); scale = float(scale)
     transform = int(transform); mirror = mirror or 'none'
   physical = name in ['TEST-PHYSICAL', 'desc:Fixture Panel synthetic (']
   rejected = (state.get('reject_resize') and not physical) or (state.get('reject_restore') and physical and not disabled)
   if not rejected:
    for m in state['monitors']:
     if matches(m, name):
      m['disabled'] = disabled
      if disabled or mirror != 'none':
       source = mirror if not disabled else next((n['name'] for n in state['monitors'] if n['name'].startswith('DeskPort-')), '')
       for workspace in state.get('workspaces', []):
        if workspace['monitor'] == m['name']: workspace['monitor'] = source
      if not disabled:
       m.update(width=int(w), height=int(h), refreshRate=refresh, scale=scale, transform=transform, mirrorOf=mirror)
       if pos != 'auto': m.update(zip(['x', 'y'], map(int, pos.split('x'))))
 else: result = 'unknown request'
 p.write_text(json.dumps(state))
 if args[:3] == ['output', 'create', 'headless']:
  time.sleep(float(os.environ.get('DESKPORT_TEST_HYPR_CREATE_DELAY', '0')))
 print(result)
'''


def line(process, timeout=8):
    ready, _, _ = select.select([process.stdout], [], [], timeout)
    assert ready, 'helper response timed out'
    value = process.stdout.readline()
    assert value, f'helper exited {process.poll()}'
    return json.loads(value)


def request(process, seq, width=1280, height=720, scale=1, **extra):
    process.stdin.write(json.dumps(dict(seq=seq, width=width, height=height, scale=scale, **dict({'displayPolicy': 2}, **extra))) + '\n')
    process.stdin.flush()
    value = line(process)
    assert value.get('seq') == seq, value
    return value


def await_clean(state, baseline):
    end = time.monotonic() + 6
    while time.monotonic() < end:
        if json.loads(state.read_text())['monitors'] == baseline:
            return
        time.sleep(.05)
    raise AssertionError(('owned output was not cleaned up or physical monitors changed', json.loads(state.read_text())['monitors'], baseline))


test_root = Path(subprocess.check_output(['bash', str(root / 'scripts/build-paths.sh'), 'checkout'], text=True).strip()) / 'tests'
test_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='hyprland-', dir=test_root) as tmp:
    directory = Path(tmp)
    helper = str(Path(args.helper).resolve()) if args.helper else str(directory / 'hyprland-helper')
    if not args.helper:
        (directory / 'main.cpp').write_text('''#include <QCoreApplication>
#include "hyprland-display.h"
int main(int argc, char** argv) {
 QCoreApplication app(argc, argv); const auto args = app.arguments();
 if(args.value(1)=="--restore-hyprland") return restoreHyprlandDisplay(args.value(2));
 return runHyprlandDisplay(1280,720);
}
''')
        (directory / 'test.pro').write_text(f'''QT = core
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = hyprland-helper
INCLUDEPATH += "{root}/host/linux"
SOURCES += "{directory}/main.cpp" "{root}/host/linux/hyprland-display.cpp"
''')
        subprocess.run(['qmake', 'test.pro'], cwd=directory, check=True, stdout=subprocess.DEVNULL)
        subprocess.run(['make', '-j2'], cwd=directory, check=True, stdout=subprocess.DEVNULL)
    if args.build_only:
        shutil.copy2(helper, args.build_only)
        raise SystemExit(0)
    binary_dir = directory / 'bin'
    binary_dir.mkdir()
    mock = binary_dir / 'hyprctl'
    mock.write_text(f'#!{sys.executable}\n' + MOCK)
    mock.chmod(0o700)
    state = directory / 'state.json'
    baseline = [dict(name='TEST-PHYSICAL', currentFormat='XRGB8888', colorManagementPreset='srgb', vrr=False, id=0, serial='synthetic', make='Fixture', model='Panel', description='Fixture Panel synthetic', width=1920, height=1080, refreshRate=60, x=12, y=24, scale=1.25, transform=1, disabled=False, mirrorOf='none')]
    baseline.append(dict(baseline[0], name='TEST-DISABLED', id=4, serial='disabled', description='Fixture Panel disabled', disabled=True, scale=1, transform=0, x=4096))
    state.write_text(json.dumps(dict(monitors=baseline, workspaces=[dict(id=7, monitor='TEST-PHYSICAL', windows=1)])))
    env = dict(os.environ, PATH=str(binary_dir) + os.pathsep + os.environ['PATH'],
               DESKPORT_TEST_HYPR_STATE=str(state), DESKPORT_DISPLAY_ON_DEMAND='1',
               XDG_RUNTIME_DIR=str(directory), XDG_CURRENT_DESKTOP='Hyprland', HYPRLAND_INSTANCE_SIGNATURE='isolated-fixture')
    count = 0
    for legacy in [False, True]:
        case_env = dict(env)
        if legacy: case_env['DESKPORT_TEST_HYPR_LEGACY'] = '1'
        process = subprocess.Popen([helper, '1280', '720'], env=case_env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            ready = line(process)
            assert ready['ready'] and not ready['active'] and ready['backend'] == 'hyprland'
            assert json.loads(state.read_text())['monitors'] == baseline
            response = request(process, 1)
            assert response['active'] and response['outputName'].startswith('DeskPort-'), response
            owned = response['outputName']
            response = request(process, 2, 1668, 2388, 2)
            assert response['width'] == 1668 and response['height'] == 2388 and response['scale'] == 2, response
            before = len([c for c in json.loads(state.read_text())['commands'] if c[0] in ['eval', 'keyword']])
            assert 'error' not in request(process, 3, 1668, 2388, 2)
            after = len([c for c in json.loads(state.read_text())['commands'] if c[0] in ['eval', 'keyword']])
            assert before == after, 'unchanged geometry unnecessarily changed the compositor'
            assert 'error' in request(process, 4, 641, 720)
            assert 'error' in request(process, 5, displayPolicy=3)
            assert not request(process, 6, session=False)['active']
            await_clean(state, baseline)
            assert request(process, 7)['outputName'] == owned
            process.stdin.close()
            assert process.wait(timeout=8) == 0
            await_clean(state, baseline)
            count += 8
        finally:
            if process.poll() is None: process.kill(); process.wait()
    # The shared policies use one immutable snapshot for geometry changes and
    # restore every originally active output through both configuration APIs.
    for legacy in [False, True]:
        for policy in [0, 1, 2]:
            case_env = dict(env)
            if legacy: case_env['DESKPORT_TEST_HYPR_LEGACY'] = '1'
            process = subprocess.Popen([helper, '1280', '720'], env=case_env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            try:
                assert line(process)['displayPolicy']
                response = request(process, 20, displayPolicy=policy)
                assert response.get('active'), response
                current = json.loads(state.read_text())['monitors'][0]
                assert current['disabled'] == (policy == 1), current
                assert current['mirrorOf'] == (response['outputName'] if policy == 0 else 'none'), current
                # A new display is never a target of the existing lease.
                added = dict(baseline[0], name='HOTPLUG', serial='new', description='Fixture Panel new', id=3)
                value = json.loads(state.read_text()); value['monitors'].append(added); state.write_text(json.dumps(value))
                assert 'error' not in request(process, 21, 1668, 2388, 2, displayPolicy=policy)
                assert json.loads(state.read_text())['monitors'][-1] == added
                # A detached original must not block release, nor retain a
                # temporary disable/mirror rule for its later return.
                value = json.loads(state.read_text()); value['monitors'] = [m for m in value['monitors'] if m['name'] != 'TEST-PHYSICAL']; state.write_text(json.dumps(value))
                assert not request(process, 22, session=False, displayPolicy=policy)['active']
                await_clean(state, [baseline[1], added])
                value = json.loads(state.read_text()); value['workspaces'] = [dict(id=7, monitor='TEST-PHYSICAL', windows=1)]; state.write_text(json.dumps(value))
                process.stdin.close(); assert process.wait(timeout=8) == 0
                value = json.loads(state.read_text()); value['monitors'] = baseline; state.write_text(json.dumps(value))
                count += 5
            finally:
                if process.poll() is None: process.kill(); process.wait()
    # Stable description selectors must not reconfigure a replacement device
    # on the old connector, including serial-prefix collisions. The original
    # device and its workspace may return on another connector during the lease.
    for legacy in [False, True]:
        for policy in [0, 1]:
            case_env = dict(env)
            if legacy: case_env['DESKPORT_TEST_HYPR_LEGACY'] = '1'
            process = subprocess.Popen([helper, '1280', '720'], env=case_env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            try:
                assert line(process)['displayPolicy']
                request(process, 25, displayPolicy=policy)
                replacement = dict(baseline[0], id=5, serial='synthetic2', description='Fixture Panel synthetic2', width=2560, height=1440, scale=1, transform=0, x=0, y=0)
                returned = dict(baseline[0], id=6, name='TEST-RETURNED')
                value = json.loads(state.read_text())
                value['monitors'] = [replacement, *[m for m in value['monitors'] if m['name'] != 'TEST-PHYSICAL'], returned]
                value['workspaces'].append(dict(id=9, monitor='TEST-PHYSICAL', windows=1))
                state.write_text(json.dumps(value))
                response = request(process, 26, 1668, 2388, 2, displayPolicy=policy)
                assert response.get('active') and not response.get('layoutWarning'), response
                assert json.loads(state.read_text())['monitors'][0] == replacement
                assert not request(process, 27, session=False, displayPolicy=policy)['active']
                await_clean(state, [replacement, baseline[1], returned])
                assert json.loads(state.read_text())['workspaces'] == [dict(id=7, monitor='TEST-RETURNED', windows=1), dict(id=9, monitor='TEST-PHYSICAL', windows=1)]
                process.stdin.close(); assert process.wait(timeout=8) == 0
                value = json.loads(state.read_text()); value.update(monitors=baseline, workspaces=[dict(id=7, monitor='TEST-PHYSICAL', windows=1)]); state.write_text(json.dumps(value))
                count += 1
            finally:
                if process.poll() is None: process.kill(); process.wait()
    # Empty transient workspace IDs may be recycled during monitor migration.
    # They must not pin recovery or move a replacement workspace to an old screen.
    for initial_windows in [0, 1]:
        value = json.loads(state.read_text()); value['workspaces'] = [dict(id=7, monitor='TEST-PHYSICAL', windows=initial_windows)]; state.write_text(json.dumps(value))
        process = subprocess.Popen([helper, '1280', '720'], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        line(process); request(process, 28, displayPolicy=1)
        value = json.loads(state.read_text())
        added = dict(baseline[0], name='TEST-NEW', id=9, serial='new', description='Fixture Panel new')
        value['monitors'].append(added)
        value['workspaces'] = [dict(id=7, monitor='TEST-NEW', windows=0)]
        state.write_text(json.dumps(value))
        assert not request(process, 29, session=False, displayPolicy=1)['active']
        await_clean(state, [*baseline, added])
        assert json.loads(state.read_text())['workspaces'] == [dict(id=7, monitor='TEST-NEW', windows=0)]
        process.stdin.close(); assert process.wait(timeout=8) == 0
        value = json.loads(state.read_text()); value.update(monitors=baseline, workspaces=[dict(id=7, monitor='TEST-PHYSICAL', windows=1)]); state.write_text(json.dumps(value))
        count += 1
    # Killing the guardian itself must replay its persisted journal before a
    # subsequent helper becomes ready. No PID-based monitor ownership is used.
    process = subprocess.Popen([helper, '1280', '720'], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    line(process); request(process, 30, displayPolicy=1)
    lock = next(directory.glob('deskport-hyprland/*/lease.lock'))
    guard = int(lock.read_text().splitlines()[0]); os.kill(guard, signal.SIGKILL)
    assert 'error' in line(process)
    assert process.wait(timeout=15) == 1
    await_clean(state, baseline)
    assert not list(directory.glob('deskport-hyprland/*/recovery.json'))
    count += 1
    # An unacknowledged physical restoration retains its journal. The next
    # guardian must replay it before readiness, rather than taking a new snapshot.
    guard_name = 'DeskPort-00000000-0000-0000-0000-000000000001'
    process = subprocess.Popen([helper, '--restore-hyprland', guard_name], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    assert line(process)['ready']
    process.stdin.write(json.dumps(dict(width=1280, height=720, scale=1, displayPolicy=1)) + '\n'); process.stdin.flush()
    assert line(process)['ok']
    value = json.loads(state.read_text()); value['reject_restore'] = True; state.write_text(json.dumps(value))
    process.stdin.close(); assert process.wait(timeout=20) == 1
    assert list(directory.glob('deskport-hyprland/*/recovery.json'))
    # Pending local repair does not prevent a new verified capture lease. The
    # original journal is retained, and a layout warning reaches the caller.
    process = subprocess.Popen([helper, '--restore-hyprland', guard_name], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    assert line(process)['ready']
    process.stdin.write(json.dumps(dict(width=1668, height=2388, scale=2, displayPolicy=2)) + '\n'); process.stdin.flush()
    assert line(process)['layoutWarning']
    process.stdin.close(); assert process.wait(timeout=20) == 1
    assert list(directory.glob('deskport-hyprland/*/recovery.json'))
    count += 1
    value = json.loads(state.read_text()); value['reject_restore'] = False; state.write_text(json.dumps(value))
    process = subprocess.Popen([helper, '1280', '720'], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    assert line(process)['ready']; await_clean(state, baseline)
    assert json.loads(state.read_text())['workspaces'] == [dict(id=7, monitor='TEST-PHYSICAL', windows=1)]
    process.stdin.close(); assert process.wait(timeout=8) == 0
    count += 2
    # Advanced color/VRR cannot be faithfully round-tripped by monitor rules.
    # Capability negotiation must retain extension without claiming topology.
    value = json.loads(state.read_text()); value['monitors'][0]['colorManagementPreset'] = 'icc'; state.write_text(json.dumps(value))
    process = subprocess.Popen([helper, '1280', '720'], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    assert not line(process)['displayPolicy']
    assert 'error' in request(process, 39, displayPolicy=0)
    limited = request(process, 40, displayPolicy=2)
    assert limited['active'] and limited['layoutWarning']
    assert json.loads(state.read_text())['monitors'][0]['colorManagementPreset'] == 'icc'
    process.stdin.close(); assert process.wait(timeout=8) == 0
    value = json.loads(state.read_text()); value['monitors'] = baseline; state.write_text(json.dumps(value))
    count += 3
    value = json.loads(state.read_text()); value['monitors'][0]['serial'] = ''; state.write_text(json.dumps(value))
    process = subprocess.Popen([helper, '1280', '720'], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    assert not line(process)['displayPolicy']
    assert request(process, 41, displayPolicy=2)['layoutWarning']
    process.stdin.close(); assert process.wait(timeout=8) == 0
    value = json.loads(state.read_text()); value['monitors'] = baseline; state.write_text(json.dumps(value))
    count += 1
    # A helper killed without destructors still releases only its own output.
    process = subprocess.Popen([helper, '1280', '720'], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    line(process); request(process, 8, displayPolicy=1)
    process.kill(); process.wait()
    await_clean(state, baseline)
    count += 1
    # Killing the guardian before its control child has sent a creation command
    # must not allow that orphan to create an unjournaled output after recovery.
    marker = state.with_suffix('.creating'); marker.unlink(missing_ok=True)
    process = subprocess.Popen([helper, '1280', '720'], env=dict(env, DESKPORT_TEST_HYPR_PRE_CREATE_DELAY='1'),
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    line(process)
    process.stdin.write(json.dumps(dict(seq=83, width=1280, height=720, scale=1, displayPolicy=2)) + '\n'); process.stdin.flush()
    deadline = time.monotonic() + 5
    while not marker.exists() and time.monotonic() < deadline: time.sleep(.01)
    assert marker.exists()
    guard = int(next(directory.glob('deskport-hyprland/*/lease.lock')).read_text().splitlines()[0])
    os.kill(guard, signal.SIGKILL)
    assert 'error' in line(process); assert process.wait(timeout=15) == 1
    time.sleep(1)
    await_clean(state, baseline); count += 1
    # The owner can die before the guard has sent its ready acknowledgment.
    process = subprocess.Popen([helper, '1280', '720'], env=dict(env, DESKPORT_TEST_HYPR_CREATE_DELAY='0.8'),
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    line(process)
    process.stdin.write(json.dumps(dict(seq=80, width=1280, height=720, scale=1, displayPolicy=2)) + '\n'); process.stdin.flush()
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if len(json.loads(state.read_text())['monitors']) > len(baseline): break
        time.sleep(.01)
    else: raise AssertionError('delayed output creation did not start')
    process.kill(); process.wait(); process.stdout.close()
    await_clean(state, baseline)
    count += 1
    process = subprocess.Popen([helper, '1280', '720'], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               text=True, start_new_session=True)
    line(process); request(process, 81, displayPolicy=0)
    os.killpg(process.pid, signal.SIGTERM); process.wait(timeout=8)
    await_clean(state, baseline)
    count += 1
    process = subprocess.Popen([helper, '1280', '720'], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               text=True, start_new_session=True)
    line(process); request(process, 82, displayPolicy=1)
    os.killpg(process.pid, signal.SIGKILL); process.wait(timeout=8)
    await_clean(state, baseline)
    count += 1
    # Compositor loss must stop capture, not silently select another monitor.
    process = subprocess.Popen([helper, '1280', '720'], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    line(process); request(process, 9)
    value = json.loads(state.read_text()); value['monitors'] = baseline; state.write_text(json.dumps(value))
    assert 'error' in line(process)
    assert process.wait(timeout=8) == 1
    await_clean(state, baseline)
    count += 1
    # A successful IPC submission with an unacknowledged mode is not success.
    value = json.loads(state.read_text()); value['reject_resize'] = True; state.write_text(json.dumps(value))
    process = subprocess.Popen([helper, '1280', '720'], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    line(process)
    assert 'error' in request(process, 10, 1920, 1080)
    assert process.wait(timeout=8) == 1
    await_clean(state, baseline)
    count += 1
    # A syntactically valid but malformed recovery record must not authorize
    # any compositor mutation or be overwritten by a new lease snapshot.
    journal = next(directory.glob('deskport-hyprland/*')) / 'recovery.json'
    journal.write_text(json.dumps(dict(version=1, output=guard_name, targets=[{}])))
    before = [c for c in json.loads(state.read_text())['commands'] if c[0] != '-j']
    process = subprocess.Popen([helper, '1280', '720'], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    assert 'error' in line(process); assert process.wait(timeout=8) == 1
    assert journal.exists()
    assert before == [c for c in json.loads(state.read_text())['commands'] if c[0] != '-j']
    journal.unlink(); count += 1
    print(f'{count} Hyprland adapter lifecycle checks passed (isolated IPC fixture; no real compositor)')
