#!/usr/bin/env python3
"""Real native client recovery in an explicitly owned linux-distro-lab guest.

Uses disposable identities, the production HostManager and Session, and loopback
GameStream. Never run against a personal desktop: the fault injection restarts
the guest's PipeWire service. Invoke through the lab's locked VM runner.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import ssl
import subprocess
import time
import uuid

from gi.repository import Gio, GLib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--policy', type=int, choices=(0, 1, 2), default=2)
    parser.add_argument('--fault', choices=('stop', 'kill'), default='stop')
    parser.add_argument('--adjustment', type=float, choices=(.8, 1., 1.5), default=1.)
    parser.add_argument('--expected-width', type=int, default=1280)
    parser.add_argument('--expected-height', type=int, default=800)
    parser.add_argument('--mesa-prefix', type=Path, help='Explicit Nix software graphics closure in the guest')
    args = parser.parse_args()
    assert os.environ.get('DESKPORT_OWNED_GNOME_VM') == '1'
    assert os.getuid() == 2000 and Path.home() == Path('/home/tester')
    assert subprocess.check_output(['systemd-detect-virt'], text=True).strip() in ('kvm', 'qemu')
    assert os.environ.get('XDG_SESSION_TYPE') == 'wayland'
    assert os.environ.get('WAYLAND_DISPLAY'), 'Use the live graphical session environment'
    assert (Path(os.environ['XDG_RUNTIME_DIR'])/os.environ['WAYLAND_DISPLAY']).is_socket()
    out = args.output.resolve()
    os.umask(0o077)
    out.mkdir(parents=True, exist_ok=False)
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

    def topology():
        value = bus.call_sync('org.gnome.Mutter.DisplayConfig', '/org/gnome/Mutter/DisplayConfig',
            'org.gnome.Mutter.DisplayConfig', 'GetCurrentState', None, None,
            Gio.DBusCallFlags.NONE, 5000, None).unpack()
        # Serial changes normally; compare active modes and logical layout.
        monitors = [(spec, [m[0] for m in modes if m[-1].get('is-current')]) for spec, modes, props in value[1]]
        return json.loads(json.dumps([monitors, value[2]]))

    def owner():
        return bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
            'GetNameOwner', GLib.Variant('(s)', ('org.gnome.Mutter.DisplayConfig',)), None,
            Gio.DBusCallFlags.NONE, 5000, None).unpack()[0]

    def wait(predicate, timeout=45):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(.2)
        raise AssertionError('Timed out waiting for ' + predicate.__name__)

    def write(path, value):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, indent=2))

    def identity(name):
        root = out / name
        root.mkdir()
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
            '-subj', '/CN=DeskPort GNOME fixture', '-keyout', str(root/'key.pem'),
            '-out', str(root/'cert.pem')], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        cert, key = (root/'cert.pem').read_text(), (root/'key.pem').read_text()
        config = root/'config/DeskPort/DeskPort.conf'
        config.parent.mkdir(parents=True)
        def bytearray(value):
            return '@ByteArray(' + value.replace('\\', '\\\\').replace('\n', '\\n') + ')'
        config.write_text('[General]\ncertificate=' + bytearray(cert) + '\nkey=' + bytearray(key) +
            '\nadaptiveResolution=false\ndisplayPolicy=' + str(args.policy) +
            '\ndesktopAdjustment=' + str(args.adjustment) +
            '\nmdns=false\n[host]\nshareOnLaunch=false\nstartAtLogin=false\n[ui]\nshowAfterRestart=false\n')
        env = dict(os.environ, XDG_CONFIG_HOME=str(root/'config'), XDG_DATA_HOME=str(root/'data'),
            XDG_CACHE_HOME=str(root/'cache'), TMPDIR=str(root),
            DESKPORT_CLIENT_LOG=str(root/'client.log'), DESKPORT_HOST_LOG=str(root/'host.log'),
            QT_QUICK_BACKEND='software', LIBGL_ALWAYS_SOFTWARE='1',
            QT_QPA_PLATFORM='wayland', SDL_VIDEODRIVER='wayland', GDK_BACKEND='wayland')
        if args.mesa_prefix:
            env['__EGL_VENDOR_LIBRARY_FILENAMES'] = str(args.mesa_prefix/'share/glvnd/egl_vendor.d/50_mesa.json')
            env['LIBGL_DRIVERS_PATH'] = str(args.mesa_prefix/'lib/dri')
            env['VK_ICD_FILENAMES'] = ':'.join(str(p) for p in (args.mesa_prefix/'share/vulkan/icd.d').glob('lvp*.json'))
            assert env['VK_ICD_FILENAMES'], 'Software Vulkan ICD missing'
        fingerprint = hashlib.sha256(ssl.PEM_cert_to_DER_cert(cert)).hexdigest()
        return root, env, cert, fingerprint

    baseline, compositor = topology(), owner()
    report = dict(passed=False, checks=[], policy=args.policy, fault=args.fault,
        gnome=subprocess.check_output(['gnome-shell', '--version'], text=True).strip(),
        baseline=baseline, hardware_gpu=False, physical_input=False,
        transport="native GameStream over guest loopback", workspace="fixed 1280x800 with desktop adjustment; native policy suite covers adaptation",
        adjustment=args.adjustment, expected_workspace=[args.expected_width, args.expected_height, 1])
    children, logs = [], []
    host, hostenv, hostcert, hostfp = identity('host')
    client, clientenv, clientcert, clientfp = identity('client')
    hostid = str(uuid.uuid4()).upper()
    hostdata = host/'data/DeskPort/DeskPort'
    clientdata = client/'data/DeskPort/DeskPort'
    write(hostdata/'host/state.json', {'root': {'uniqueid': hostid, 'named_devices': [
        {'uuid': str(uuid.UUID(clientfp[:32])), 'name': 'GNOME fixture', 'cert': clientcert, 'enabled': 'true'}]}})
    write(hostdata/'host/apps.json', {'env': {}, 'apps': [{'name': 'Desktop'}]})
    write(hostdata/'binding/peers.json', {'version': 1, 'peers': {clientfp: {
        'ready': True, 'granted': True, 'clientCert': clientcert, 'name': 'GNOME fixture', 'role': 'client'}}})
    write(clientdata/'binding/peers.json', {'version': 1, 'peers': {hostfp: {
        'ready': True, 'granted': True, 'clientCert': hostcert, 'hostCert': hostcert,
        'hostId': hostid, 'bindingPort': 48991, 'hostPort': 48989, 'address': '127.0.0.1'}}})
    # Seed the GameStream certificate pin as well as control-channel trust.
    # This fixture tests authenticated streaming/recovery, not the pairing UI.
    with (client/'config/DeskPort/DeskPort.conf').open('a') as config:
        config.write('[hosts]\nsize=1\n1\\hostname=GNOME fixture\n1\\uuid=' + hostid +
            '\n1\\localaddress=127.0.0.1\n1\\localport=48989\n1\\manualaddress=127.0.0.1\n'
            '1\\manualport=48989\n1\\srvcert=@ByteArray(' + hostcert.replace('\n', '\\n') + ')\n')

    def spawn(name, arguments, env):
        log = (out/(name+'.log')).open('w')
        logs.append(log)
        process = subprocess.Popen([str(args.app), *arguments], env=env, cwd=out,
            stdout=log, stderr=subprocess.STDOUT)
        children.append(process)
        return process

    def metrics():
        path = client/'client.log'
        text = path.read_text(errors='replace') if path.exists() else ''
        return [dict(received=int(a), rendered=int(b)) for a, b in
                re.findall(r'received_fps=(\d+) rendered_fps=(\d+)', text)]

    try:
        supervisor = spawn('supervisor', ['host', 'run', '--no-web-server'], hostenv)
        # Listener readiness is observable without relying on translated status.
        def listener_ready():
            import socket
            try:
                with socket.create_connection(('127.0.0.1', 48989), timeout=.2):
                    return True
            except OSError:
                return False
        wait(listener_ready)
        viewer = spawn('viewer', ['stream', '127.0.0.1:48989', 'Desktop', '--resolution', '1280x800',
            '--fps', '30', '--bitrate', '3000', '--video-decoder', 'software', '--video-codec', 'H.264',
            '--display-mode', 'borderless' if args.policy == 0 else 'windowed', '--no-vsync'], clientenv)
        def frames_received():
            assert viewer.poll() is None, 'Native client exited before receiving frames'
            return len([m for m in metrics() if m['rendered'] > 0]) >= 3
        wait(frames_received, 70)
        descriptor = hostdata/'host/virtual-display.json'
        before = json.loads(descriptor.read_text())
        assert [before['width'], before['height'], before['scale']] == report['expected_workspace'], before
        report.update(client_pid=viewer.pid, supervisor_pid=supervisor.pid, before=before,
                      before_metrics=metrics())
        report['checks'].append('Native client rendered sustained GameStream frames from the owned GNOME output')
        fault_time = time.monotonic()
        count_before = len(metrics())
        if args.fault == 'kill':
            subprocess.run(['systemctl', '--user', 'kill', '--signal=SIGKILL', '--kill-whom=main', 'pipewire.service'], check=True, timeout=20)
        else:
            subprocess.run(['systemctl', '--user', 'stop', 'pipewire.socket', 'pipewire.service'], check=True, timeout=20)
            time.sleep(2)
        subprocess.run(['systemctl', '--user', 'start', 'pipewire.socket', 'pipewire.service', 'wireplumber.service'], check=True, timeout=20)
        def resumed_frames():
            assert viewer.poll() is None and supervisor.poll() is None, 'Original client/supervisor exited'
            return len([m for m in metrics()[count_before:] if m['rendered'] > 0]) >= 3
        wait(resumed_frames, 70)
        after = json.loads(descriptor.read_text())
        assert [after['width'], after['height'], after['scale']] == report['expected_workspace'], after
        assert after != before, 'Recovery did not create a fresh capture identity'
        assert owner() == compositor, 'Compositor restarted during recovery'
        report.update(after=after, after_metrics=metrics()[count_before:],
                      recovery_seconds=round(time.monotonic()-fault_time, 2))
        report['checks'].append('Same native client and supervisor automatically resumed sustained rendered frames')
        report['checks'].append('Fresh owned capture identity without compositor restart')
        report['passed'] = True
    except BaseException as error:
        report['failure'] = repr(error)
    finally:
        subprocess.run(['systemctl', '--user', 'start', 'pipewire.socket', 'pipewire.service', 'wireplumber.service'], timeout=20)
        for process in reversed(children):
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=35)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
                    report['passed'] = False
                    report['cleanup_failure'] = 'Owned process required SIGKILL'
        for log in logs:
            log.close()
        try:
            wait(lambda: topology() == baseline, 25)
            report['checks'].append('Exact local display layout restored after shutdown')
        except BaseException as error:
            report.update(passed=False, cleanup_failure=repr(error))
        report['children_stopped'] = all(p.poll() is not None for p in children)
        try:
            report['compositor_unchanged'] = owner() == compositor
        except Exception as error:
            report.update(passed=False, compositor_unchanged=False, compositor_failure=repr(error))
        write(out/'result.json', report)
        print(json.dumps(report, indent=2), flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
