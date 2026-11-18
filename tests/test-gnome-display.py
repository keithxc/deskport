#!/usr/bin/env python3
"""Native Mutter lease checks. Run only in an owned synthetic GNOME VM/session."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import json
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import time
from gi.repository import Gio, GLib

assert os.environ.get('DESKPORT_GNOME_TEST_SESSION') == '1', 'Reserve an isolated GNOME session first'
def timed_out(_signal,_frame):
    raise TimeoutError('Native GNOME scenario exceeded 300 seconds')
signal.signal(signal.SIGALRM,timed_out)
signal.alarm(300)
helper = str(Path(sys.argv[1]).resolve())
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

if os.environ.get('DESKPORT_GNOME_HEADLESS_TEST') != '1' and '--input-fixture' not in sys.argv:
    # DisplayConfig is exported before Shell finishes its startup animation.
    # Changing outputs then can strand Shell's transparent reactive cover pane,
    # making a focused test window accept keys but never pointer input.
    shell_pid=bus.call_sync('org.freedesktop.DBus','/org/freedesktop/DBus',
        'org.freedesktop.DBus','GetConnectionUnixProcessID',GLib.Variant('(s)',('org.gnome.Shell',)),
        None,Gio.DBusCallFlags.NONE,5000,None).unpack()[0]
    deadline=time.monotonic()+60
    while True:
        started=subprocess.run(['journalctl','--user','-b',f'_PID={shell_pid}',
            'MESSAGE_ID=f3ea493c22934e26811cd62abe8e203a','-n','1','-o','json','--no-pager'],
            capture_output=True,text=True,timeout=5)
        if started.returncode==0 and started.stdout.strip().startswith('{'):break
        assert time.monotonic()<deadline,'GNOME Shell startup animation did not complete'
        time.sleep(.2)

def call(method, params=None):
    return bus.call_sync('org.gnome.Mutter.DisplayConfig', '/org/gnome/Mutter/DisplayConfig',
                         'org.gnome.Mutter.DisplayConfig', method, params, None,
                         Gio.DBusCallFlags.NONE, 5000, None).unpack()

def state():
    serial, monitors, logical, properties = call('GetCurrentState')
    return dict(serial=serial, monitors=monitors, logical=logical, properties=properties)

def topology():
    current = state()
    monitors = {tuple(spec): (modes, props) for spec, modes, props in current['monitors']}
    result = {}
    for x, y, scale, transform, primary, group, _ in current['logical']:
        for spec in group:
            modes, props = monitors[tuple(spec)]
            mode = next(m for m in modes if m[-1].get('is-current'))
            result[spec[0]] = dict(spec=spec, x=x, y=y, scale=scale, transform=transform,
                                   primary=primary, pixels=mode[1:3], refresh=mode[3],
                                   group=sorted(s[0] for s in group))
    return result

def until(predicate, timeout=12):
    deadline = time.monotonic()+timeout
    while time.monotonic() < deadline:
        if predicate(): return
        time.sleep(.05)
    raise AssertionError('Timed out: '+json.dumps(topology()))

def exited(pid):
    try:return Path('/proc/'+pid+'/stat').read_text().rsplit(') ',1)[1].startswith('Z')
    except (FileNotFoundError,ProcessLookupError):return True

baseline = topology()
assert baseline, 'A synthetic local output is required'
width, height = next(iter(baseline.values()))['pixels']
scale = next(iter(baseline.values()))['scale']
assert width % 4 == height % 4 == 0 and scale in (1, 2)
children = []
foreign_children = []
report = dict(gnome=subprocess.check_output([os.environ.get('DESKPORT_GNOME_SHELL','gnome-shell'),'--version'], text=True).strip(),
              baseline=baseline, checks=[], requests=[], passed=False, hardware_gpu=False, production_stream_input=False)

def checkpoint():
    # GDK terminates the process in C when the compositor disappears, bypassing
    # Python's finally block. Keep the last completed check and pending request.
    temporary=Path('progress.json.tmp')
    temporary.write_text(json.dumps(report,indent=2)+'\n')
    temporary.replace('progress.json')

class Checks(list):
    def append(self, value):
        super().append(value)
        checkpoint()
report['checks']=Checks()
checkpoint()

scale_cases=[]
if '--workspace-fixtures' in sys.argv:
    fixtures=Path(sys.argv[sys.argv.index('--workspace-fixtures')+1])
    names={'standard','retina','fractional','slight-density','portrait'}
    scale_cases=[case for case in json.loads(fixtures.read_text()) if case['name'] in names]
    assert len(scale_cases)==len(names),'Shared workspace fixture set is incomplete'
    report['shared_workspace_cases']=scale_cases

def launch(expected=None):
    log = open(Path.cwd()/f'helper-{len(children)}.log','w')
    p = subprocess.Popen([helper,'1280','720'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=log, text=True, env=dict(os.environ,DESKPORT_DISPLAY_ON_DEMAND='1'))
    children.append((p,log))
    assert receive(p).get('ready')
    assert topology() == (baseline if expected is None else expected)
    return p

reply_buffers={}
def receive(p):
    deadline=time.monotonic()+15
    while True:
        buffer=reply_buffers.get(p.pid,b'')
        if b'\n' not in buffer:
            assert select.select([p.stdout],[],[],max(0,deadline-time.monotonic()))[0], 'Helper reply timed out'
            data=os.read(p.stdout.fileno(),8192)
            assert data,f'Helper stopped: {p.poll()}'
            reply_buffers[p.pid]=buffer+data
            continue
        line,reply_buffers[p.pid]=buffer.split(b'\n',1)
        response=json.loads(line)
        if 'layoutWarning' in response and 'seq' not in response:
            report.setdefault('layout_events',[]).append(response)
            continue
        return response

def request(p, policy, w, h, backing, active=True, seq=1):
    report['pending_request']=dict(pid=p.pid,policy=policy,width=w,height=h,scale=backing,active=active,seq=seq)
    checkpoint()
    p.stdin.write(json.dumps(dict(seq=seq,width=w,height=h,scale=backing,displayPolicy=policy,session=active))+'\n')
    p.stdin.flush()
    response=receive(p)
    report['requests'].append(dict(policy=policy,width=w,height=h,scale=backing,active=active,response=response))
    report.pop('pending_request',None)
    checkpoint()
    return response

def restored(p):
    assert p.wait(timeout=20) == 0
    until(lambda:topology() == baseline)

def verify(reply, policy, w, h, backing):
    assert 'error' not in reply, reply
    current = topology(); owned = current.pop(reply['outputName'])
    assert owned['pixels'] == (w,h) and owned['scale'] == backing, (reply,owned)
    if policy == 2: assert current == baseline, (current,baseline)
    else:
        assert owned['primary'] and (owned['x'],owned['y']) == (0,0)
        if policy == 1: assert not current, current
        else:
            assert set(current) == set(baseline)
            assert owned['group'] == sorted([reply['outputName'],*baseline])
            for name,value in current.items():
                assert value['pixels'] == baseline[name]['pixels'] and value['scale'] == baseline[name]['scale']
    return reply['outputName']

def capture_probe(identity,w,h,backing,missing=False):
    manifest=json.loads(Path('manifest.json').read_text()) if Path('manifest.json').exists() else {}
    host=os.environ.get('DESKPORT_TEST_HOST') or manifest.get('host')
    if not host:
        report['sunshine_capture']='UNTESTED: supply DESKPORT_TEST_HOST'
        return
    work=Path.cwd()/'capture';work.mkdir(exist_ok=True)
    descriptor=work/'virtual-display.json'
    descriptor.write_text(json.dumps(dict(node=identity['pipewireNode'],serial=identity['pipewireSerial'],
                                         output=identity['outputName'],width=w,height=h,scale=backing)))
    config=work/'sunshine.conf'
    config.write_text(f'capture = portal\noutput_name = {identity["outputName"]}\nencoder = software\n'
                      'keyboard = disabled\nmouse = disabled\ncontroller = disabled\nstream_audio = disabled\n'
                      'upnp = disabled\nsystem_tray = disabled\nbind_address = 127.0.0.1\nport = 57989\nmin_log_level = 1\n'
                      + ''.join(f'{key} = {work/name}\n' for key,name in (
                          ('file_state','state.json'),('credentials_file','control.json'),('pkey','key.pem'),
                          ('cert','cert.pem'),('file_apps','apps.json'),('log_path','sunshine.log'))))
    (work/'apps.json').write_text('{"apps": []}')
    logpath=work/f'probe-{len(report["checks"])}.log'
    capture_env=dict(os.environ,DESKPORT_VIRTUAL_DISPLAY=str(descriptor),DBUS_SYSTEM_BUS_ADDRESS='unix:path='+str(work/'no-system-bus'))
    if manifest.get('mesa'):
        mesa=Path(manifest['mesa'])
        capture_env.update(__EGL_VENDOR_LIBRARY_FILENAMES=str(mesa/'share/glvnd/egl_vendor.d/50_mesa.json'),
                           LIBGL_DRIVERS_PATH=str(mesa/'lib/dri'),LIBGL_ALWAYS_SOFTWARE='1',GALLIUM_DRIVER='llvmpipe',EGL_PLATFORM='surfaceless')
    with logpath.open('w') as log:
        process=subprocess.Popen([host,str(config)],cwd=work,stdout=log,stderr=subprocess.STDOUT,
            env=capture_env)
        try:
            deadline=time.monotonic()+25
            while time.monotonic()<deadline:
                text=logpath.read_text(errors='replace')
                if ('Found H.264 encoder:' in text or 'refusing physical display fallback' in text or
                    'Video failed to find working encoder' in text or process.poll() is not None):break
                time.sleep(.1)
            if time.monotonic() >= deadline and process.poll() is None:
                debugger=os.environ.get('DESKPORT_TEST_GDB')
                if debugger:
                    trace=subprocess.run(['sudo','-n',debugger,'-batch','-ex','set debuginfod enabled off',
                        '-ex','set pagination off','-ex','thread apply all bt','-p',str(process.pid)],
                        capture_output=True,text=True,timeout=15)
                    report.setdefault('capture_thread_stacks',[]).append(trace.stdout+trace.stderr)
        finally:
            if process.poll() is None:
                process.terminate()
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)
    text=logpath.read_text(errors='replace')
    report.setdefault('capture_logs',[]).append(text[-12000:])
    if missing:
        assert 'Found H.264 encoder:' not in text and (
            'refusing physical display fallback' in text or
            f"Could not find display with name: '{identity['outputName']}'" in text),text[-12000:]
        report['checks'].append('Sunshine refused physical fallback after owned stream removal')
    else:
        assert 'Found H.264 encoder:' in text and f'DeskPort owned virtual capture: {identity["outputName"]} {w}x{h}' in text,text[-12000:]
        report['checks'].append(f'Sunshine owned PipeWire capture and software H.264 encoder at {w}x{h}@{backing}')

def capture(identity,w,h,backing,missing=False):
    # Capture runtime failures do not suppress independent topology/recovery
    # evidence. The whole run still fails after recording the remaining checks.
    if report.get('capture_failures'):return
    try:capture_probe(identity,w,h,backing,missing)
    except (AssertionError,OSError,subprocess.TimeoutExpired) as error:
        report.setdefault('capture_failures',[]).append(str(error)[-12000:])

def input_fixture(owned):
    # Test Mutter's platform input path against a guest-only GTK window. This
    # deliberately does not claim Sunshine transport/uinput acceptance.
    os.environ.setdefault('GSK_RENDERER','cairo')
    import gi
    gi.require_version('Gtk','4.0')
    from gi.repository import Gtk, Gdk
    window=Gtk.Window(title='DeskPort synthetic input fixture')
    entry=Gtk.Entry()
    box=Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
    box.append(entry)
    target=Gtk.Button(label='DeskPort synthetic pointer target');target.set_vexpand(True)
    box.append(target);window.set_child(box)
    clicks=[]
    gesture=Gtk.GestureClick.new(); gesture.set_propagation_phase(Gtk.PropagationPhase.CAPTURE)
    gesture.connect('pressed',lambda _g,_n,x,y:clicks.append((x,y)))
    window.add_controller(gesture)
    motions=[]
    motion=Gtk.EventControllerMotion.new()
    motion.connect('motion',lambda _m,x,y:motions.append((x,y)))
    window.add_controller(motion)
    def pump(seconds):
        deadline=time.monotonic()+seconds
        context=GLib.MainContext.default()
        while time.monotonic()<deadline:
            while context.pending():context.iteration(False)
            time.sleep(.01)
    session=None
    def remote(method,params=None,path=None,interface='org.gnome.Mutter.RemoteDesktop.Session'):
        return bus.call_sync('org.gnome.Mutter.RemoteDesktop',path or session,interface,
                             method,params,None,Gio.DBusCallFlags.NONE,5000,None).unpack()
    try:
        window.fullscreen();window.present();entry.grab_focus();pump(1)
        deadline=time.monotonic()+5
        while time.monotonic()<deadline and (not window.is_fullscreen() or not window.is_active()):pump(.05)
        assert window.is_fullscreen() and window.is_active(),'Synthetic input window did not finish mapping'
        surface=window.get_surface()
        monitor=Gdk.Display.get_default().get_monitor_at_surface(surface)
        assert monitor.get_connector() == owned, monitor.get_connector()
        output=topology()[owned]
        expected=[round(v/output['scale']) for v in output['pixels']]
        assert [window.get_width(),window.get_height()] == expected,expected
        assert window.get_scale_factor() == output['scale'],output
        session=remote('CreateSession',path='/org/gnome/Mutter/RemoteDesktop',interface='org.gnome.Mutter.RemoteDesktop')[0]
        session_id=remote('Get',GLib.Variant('(ss)',('org.gnome.Mutter.RemoteDesktop.Session','SessionId')),
                          interface='org.freedesktop.DBus.Properties')[0]
        def screencast(path,interface,method,params):
            return bus.call_sync('org.gnome.Mutter.ScreenCast',path,interface,method,params,None,
                                 Gio.DBusCallFlags.NONE,5000,None).unpack()[0]
        cast=screencast('/org/gnome/Mutter/ScreenCast','org.gnome.Mutter.ScreenCast','CreateSession',
                        GLib.Variant('(a{sv})',({'remote-desktop-session-id':GLib.Variant('s',session_id)},)))
        stream=screencast(cast,'org.gnome.Mutter.ScreenCast.Session','RecordMonitor',
                          GLib.Variant('(sa{sv})',(owned,{'cursor-mode':GLib.Variant('u',0)})))
        remote('Start')
        # Use the owned stream's pixel coordinates, not an accelerated relative
        # warp or the Shell's hot corners. Verify Mutter's 2x input transform.
        pump(.2)
        remote('NotifyPointerMotionAbsolute',GLib.Variant('(sdd)',(stream,output['pixels'][0]*.4,output['pixels'][1]*.6)))
        pump(.2)
        pointer=Gdk.Display.get_default().get_default_seat().get_pointer()
        pointed,px,py=pointer.get_surface_at_position()
        diagnostic=dict(logical=[window.get_width(),window.get_height()],
                        scale=window.get_scale_factor(), pointer=[px,py],
                        pointer_on_fixture=pointed==surface, motions=motions[-10:])
        assert pointed==surface and abs(px-expected[0]*.4)<2 and abs(py-expected[1]*.6)<2,diagnostic
        remote('NotifyPointerButton',GLib.Variant('(ib)',(272,True)))
        pump(.1)
        remote('NotifyPointerButton',GLib.Variant('(ib)',(272,False)))
        pump(.3);entry.grab_focus()
        deadline=time.monotonic()+5
        while time.monotonic()<deadline and (not window.is_active() or window.get_focus() is None):pump(.05)
        assert window.is_active() and window.get_focus() is not None,'Synthetic input window did not acquire keyboard focus'
        pump(.2)
        for key in 'deskport':
            remote('NotifyKeyboardKeysym',GLib.Variant('(ub)',(ord(key),True)))
            pump(.03)
            remote('NotifyKeyboardKeysym',GLib.Variant('(ub)',(ord(key),False)))
            pump(.03)
        deadline=time.monotonic()+5
        while time.monotonic()<deadline and entry.get_text() != 'deskport':pump(.05)
        assert entry.get_text() == 'deskport' and clicks,(entry.get_text(),clicks,diagnostic)
        report['checks'].append('Mutter platform input: synthetic GTK window received pixel-mapped click and fixture text at verified native scale')
    finally:
        if session:remote('Stop')
        window.destroy();pump(.2)

if '--input-fixture' in sys.argv:
    input_fixture(sys.argv[sys.argv.index('--input-fixture')+1])
    sys.exit(0)

def synthetic_input(owned):
    # GTK owns a Wayland connection and requires its event loop to keep running.
    # Keep it in a bounded child so later blocking topology/capture stress cannot
    # strand a dormant GTK connection or lose the controller's failure report.
    directory=Path.cwd()/'input-fixture'
    directory.mkdir(exist_ok=True)
    report['input_fixtures']=[]
    for _ in range(3):
        result=subprocess.run([sys.executable,str(Path(__file__).resolve()),helper,'--input-fixture',owned],
                              cwd=directory,capture_output=True,text=True,timeout=20)
        report['input_fixture']=dict(returncode=result.returncode,stdout=result.stdout,stderr=result.stderr)
        report['input_fixtures'].append(report['input_fixture'])
        assert result.returncode==0,report['input_fixture']
    current=topology()[owned]
    report.setdefault('native_scale_input',[]).append(dict(pixels=current['pixels'],
        scale=current['scale'],logical=[round(v/current['scale']) for v in current['pixels']],repetitions=3))
    report['checks'].append('Mutter platform input: three synthetic GTK windows received pixel-mapped clicks and fixture text at verified native scale')

try:
    for policy in (2,1,0):
        p=launch()
        identity=request(p,policy,width,height,scale)
        owned=verify(identity,policy,width,height,scale)
        capture(identity,width,height,scale)
        if policy==1 and scale_cases:synthetic_input(owned)
        if policy != 0:
            modes=[(1668,2388,2),(1920,1080,1),(2560,1440,2)]
            modes.extend(tuple(case['qt']) for case in scale_cases)
            for seq,(w,h,backing) in enumerate(modes,2):
                reply=request(p,policy,w,h,backing,seq=seq)
                reply.setdefault('outputName',owned)
                assert verify(reply,policy,w,h,backing) == owned
                capture(identity,w,h,backing)
                if policy==1 and scale_cases:synthetic_input(owned)
        if policy == 1 and not scale_cases:
            if os.environ.get('DESKPORT_GNOME_HEADLESS_TEST') == '1':
                report['platform_input']='UNTESTED: headless fixture has no interactive desktop focus; use the full VM fixture'
            else:synthetic_input(owned)
        release=request(p,policy,width,height,scale,False,seq=10)
        assert 'error' not in release and not release['active'], release
        until(lambda:topology() == baseline)
        # A new lease gets a fresh snapshot and an independent PipeWire target.
        resumed=request(p,policy,width,height,scale,seq=11)
        verify(resumed,policy,width,height,scale)
        p.stdin.close(); restored(p)
        report['checks'].append(f'policy-{policy}: native topology, scale, release, recreate, EOF')
    for policy in (0,1,2):
        p=launch();identity=request(p,policy,width,height,scale)
        verify(identity,policy,width,height,scale)
        guards=Path(f'/proc/{p.pid}/task/{p.pid}/children').read_text().split()
        assert guards, 'Independent recovery process missing'
        p.kill(); p.wait(timeout=5)
        until(lambda:topology() == baseline)
        until(lambda:all(exited(pid) for pid in guards))
        if policy == 1:capture(identity,width,height,scale,missing=True)
        report['checks'].append(f'policy-{policy}: SIGKILL restored physical layout and guardian exited')
    # Connector names may be recycled immediately. Each acknowledged lease must
    # nevertheless identify a fresh capture stream, and release must finish
    # removal before another admission snapshot is taken.
    p=launch(); serials=set()
    for cycle in range(20):
        w,h,backing=((1280,720,1),(1668,2388,2))[cycle % 2]
        identity=request(p,2,w,h,backing,seq=100+cycle*2)
        verify(identity,2,w,h,backing)
        assert identity['pipewireSerial'] not in serials, identity
        serials.add(identity['pipewireSerial'])
        release=request(p,2,w,h,backing,False,seq=101+cycle*2)
        assert not release.get('error') and not release['active'],release
        until(lambda:topology()==baseline)
    p.stdin.close();restored(p)
    report['checks'].append('20 alternating landscape/portrait leases: fresh PipeWire serials, synchronous removal and unchanged physical layout')
    p=launch();identity=request(p,1,width,height,scale)
    verify(identity,1,width,height,scale)
    guards=Path(f'/proc/{p.pid}/task/{p.pid}/children').read_text().split()
    assert len(guards)==1,guards
    os.kill(int(guards[0]),signal.SIGKILL)
    assert p.wait(timeout=20)!=0, 'Guardian death must stop the display owner'
    until(lambda:topology()==baseline)
    journals=list((Path(os.environ['XDG_RUNTIME_DIR'])/'DeskPort').glob('mutter-*.json'))
    assert journals,'Guardian death must retain its recovery snapshot'
    p=launch();verify(request(p,1,width,height,scale),1,width,height,scale)
    p.stdin.close();restored(p)
    assert not list((Path(os.environ['XDG_RUNTIME_DIR'])/'DeskPort').glob('mutter-*.json'))
    report['checks'].append('guardian SIGKILL restored local control; next lease replayed and verified pending journal')
    # Simulate a saved physical mode which is no longer advertised. The old
    # checkpoint must survive a new verified workspace instead of blocking it.
    p=launch();verify(request(p,1,width,height,scale),1,width,height,scale)
    guard=Path(f'/proc/{p.pid}/task/{p.pid}/children').read_text().split()
    assert len(guard)==1
    os.kill(int(guard[0]),signal.SIGKILL);assert p.wait(timeout=20)!=0
    until(lambda:topology()==baseline)
    journal=next((Path(os.environ['XDG_RUNTIME_DIR'])/'DeskPort').glob('mutter-*.json'))
    saved=json.loads(journal.read_text())
    broken=json.loads(json.dumps(saved));broken['logical'][0]['monitors'][0]['mode']['width']=9992
    journal.write_text(json.dumps(broken))
    p=launch();response=request(p,2,width,height,scale)
    verify(response,2,width,height,scale)
    assert response.get('layoutWarning'),response
    retained=json.loads(journal.read_text())
    assert retained['logical']==broken['logical'] and retained['monitors']==saved['monitors']
    p.stdin.close();restored(p)
    assert journal.exists(),'Failed recovery checkpoint was discarded'
    repaired=json.loads(journal.read_text());repaired['logical']=saved['logical']
    journal.write_text(json.dumps(repaired))
    p=launch();response=request(p,2,width,height,scale)
    assert 'error' not in response and not response.get('layoutWarning'),response
    p.stdin.close();restored(p);assert not journal.exists()
    report['checks'].append('unavailable recovery mode retained original checkpoint, allowed a verified new workspace with warning, and recovered after fixture repair')

    # Another client's normal ScreenCast stream, sized by an independent
    # consumer. Fixed-mode RecordVirtual was added after GNOME 49, so do not
    # use that newer API as the cross-version hotplug fixture.
    def foreign_monitor():
        service='org.gnome.Mutter.ScreenCast'
        def sc(path,interface,method,params=None):
            return bus.call_sync(service,path,interface,method,params,None,Gio.DBusCallFlags.NONE,5000,None).unpack()
        before={tuple(spec) for spec,_modes,_props in state()['monitors']}
        path=sc('/org/gnome/Mutter/ScreenCast',service,'CreateSession',GLib.Variant('(a{sv})',({},)))[0]
        stream=sc(path,service+'.Session','RecordVirtual',GLib.Variant('(a{sv})',({},)))[0]
        nodes=[]
        subscription=bus.signal_subscribe(service,service+'.Stream','PipeWireStreamAdded',stream,None,
            Gio.DBusSignalFlags.NONE,lambda _c,_s,_p,_i,_n,params,*_rest:nodes.append(params.unpack()[0]))
        sc(path,service+'.Session','Start')
        def node_ready():
            while GLib.MainContext.default().iteration(False):pass
            return bool(nodes)
        until(node_ready)
        bus.signal_unsubscribe(subscription)
        sink=os.environ.get('DESKPORT_GNOME_VIDEO_SINK',str(Path(helper).with_name('pipewire-video-sink')))
        consumer=subprocess.Popen([sink,str(nodes[0]),'800','600'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        foreign_children.append(consumer)
        def added():
            return [spec[0] for spec,_modes,_props in state()['monitors'] if tuple(spec) not in before]
        until(lambda:len(added())==1)
        output=added()[0]
        def stop():
            try:sc(path,service+'.Session','Stop')
            finally:
                if consumer.poll() is None:consumer.terminate()
                consumer.wait(timeout=5)
        return output,stop
    for detach_first in (False,True):
        p=launch();identity=request(p,1,width,height,scale)
        owned=verify(identity,1,width,height,scale)
        foreign,stop=foreign_monitor()
        try:
            foreign_state=topology()[foreign]
            response=request(p,1,width,height,scale,seq=2)
            assert 'error' not in response,response
            current=topology()
            if any(name in current for name in baseline):
                assert response.get('layoutWarning'), 'A changed physical policy must report degradation'
                report['checks'].append('Mutter hotplug policy degradation reported while owned workspace remains usable')
            capture(identity,width,height,scale)
            if detach_first:
                stop();stop=None
                response=request(p,1,width,height,scale,seq=3)
                assert 'error' not in response,response
            p.stdin.close();assert p.wait(timeout=20)==0
            def recovered_hotplug():
                current=topology()
                return owned not in current and all(current.get(key)==value for key,value in baseline.items()) and (
                    foreign not in current if detach_first else foreign in current and all(
                        current[foreign][key]==foreign_state[key] for key in ('spec','pixels','scale','transform','refresh')))
            until(recovered_hotplug)
        finally:
            if stop:stop()
        until(lambda:topology()==baseline)
        report['checks'].append('hotplug detach' if detach_first else 'hotplug output preserved through lease recovery')
    foreign,stop=foreign_monitor()
    try:
        with_foreign=topology();p=launch(with_foreign)
        response=request(p,1,width,height,scale)
        assert response.get('error') and 'Another client' in response['error'],response
        p.wait(timeout=20);assert topology()==with_foreign
    finally:stop()
    until(lambda:topology()==baseline)
    report['checks'].append('pre-existing foreign virtual output refused takeover before mutation')
    # Unsupported mirroring must fail before RecordVirtual/PipeWire creation.
    p=launch(); result=request(p,0,1668,2388,2)
    assert result.get('error') and 'Mutter' in result['error'], result
    p.wait(timeout=20); until(lambda:topology() == baseline)
    report['checks'].append('unsupported mirror rejected before display mutation')
    p=launch(); request(p,2,width,height,scale)
    result=request(p,1,width,height,scale,seq=2)
    assert result.get('error') and 'policy' in result['error'],result
    p.wait(timeout=20); until(lambda:topology() == baseline)
    report['checks'].append('policy change rejected and lease recovered')
    original_state=state()
    def apply_physical(source, chosen=None):
        configs=[]
        for x,y,backing,transform,primary,group,_ in source['logical']:
            monitors=[]
            for spec in group:
                modes=next(modes for identity,modes,_props in source['monitors'] if identity==spec)
                mode=chosen or next(mode for mode in modes if mode[-1].get('is-current'))
                monitors.append((spec[0],mode[0],{}))
            configs.append((x,y,2. if chosen else backing,transform,primary,monitors))
        current=state()
        options={}
        if current['properties'].get('supports-changing-layout-mode'):
            options['layout-mode']=GLib.Variant('u',current['properties']['layout-mode'])
        params=GLib.Variant.new_tuple(GLib.Variant('u',current['serial']),GLib.Variant('u',1),
                                     GLib.Variant('a(iiduba(ssa{sv}))',configs),GLib.Variant('a{sv}',options))
        call('ApplyMonitorsConfig',params)
    candidates=[mode for _spec,modes,_props in original_state['monitors'] for mode in modes
                if mode[1]>=1600 and mode[2]>=1200 and 2. in mode[5]]
    if len(original_state['logical'])==1 and len(original_state['monitors'])==1 and candidates:
        try:
            apply_physical(original_state,sorted(candidates,key=lambda mode:mode[1]*mode[2])[0])
            changed=topology();assert next(iter(changed.values()))['scale']==2
            p=launch(changed)
            response=request(p,1,1668,2388,2)
            assert 'error' not in response,response
            p.stdin.close();assert p.wait(timeout=20)==0
            until(lambda:topology()==changed)
            report['checks'].append('non-default physical 2x scale and mode restored after primary-only lease')
        finally:
            apply_physical(original_state)
        until(lambda:topology()==baseline)
    else:report['nondefault_physical_scale']='UNTESTED: fixture has no suitable advertised 2x mode'
    # Run compositor-wide dependency loss in its own fresh VM: on affected
    # Mutter versions it can make DisplayConfig unresponsive for the session.
    if '--pipewire-loss' in sys.argv:
        def compositor_owner():
            return bus.call_sync('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus',
                'GetNameOwner',GLib.Variant('(s)',('org.gnome.Mutter.DisplayConfig',)),None,
                Gio.DBusCallFlags.NONE,5000,None).unpack()[0]
        owner=compositor_owner()
        for abrupt in (False,True):
            for policy in (2,1,0):
                # Keep independent injected faults apart: the fixture's units
                # allow five starts per ten seconds. Preserve that protection
                # instead of exhausting it with this synthetic rapid loop.
                time.sleep(5)
                p=launch();identity=request(p,policy,width,height,scale)
                verify(identity,policy,width,height,scale)
                mode='SIGKILL' if abrupt else 'service stop'
                report['dependency_loss']=dict(policy=policy,mode=mode,compositor_owner=owner)
                checkpoint()
                try:
                    if abrupt:
                        # On Ubuntu the service Requires its socket: stopping
                        # the socket also gracefully stops the service, so a
                        # subsequent kill would never test abrupt loss.
                        subprocess.run(['systemctl','--user','kill','--signal=SIGKILL','--kill-whom=main','pipewire.service'],check=True,timeout=20)
                    else:
                        subprocess.run(['systemctl','--user','stop','pipewire.socket','pipewire.service'],check=True,timeout=20)
                    assert p.wait(timeout=20)!=0,'Loss of the PipeWire service must terminate the display owner'
                    report['checks'].append(f'policy-{policy}: PipeWire {mode} terminated the display owner')
                finally:
                    subprocess.run(['systemctl','--user','start','pipewire.socket','pipewire.service','wireplumber.service'],check=True,timeout=20)
                until(lambda:topology()==baseline)
                assert compositor_owner()==owner,'Recovery must not restart or replace the compositor'
                assert not list((Path(os.environ['XDG_RUNTIME_DIR'])/'DeskPort').glob('mutter-*.json')),'Recovery journal must be verified and disarmed'
                report['checks'].append(f'policy-{policy}: PipeWire {mode} restored physical layout without compositor restart')
                p=launch();new=request(p,2,width,height,scale)
                verify(new,2,width,height,scale)
                capture(new,width,height,scale)
                p.stdin.close();restored(p)
                report['checks'].append(f'policy-{policy}: fresh native lease and capture after PipeWire {mode}')
    else:
        report['pipewire_service_loss']='UNTESTED in this run: use --pipewire-loss in a separate fresh VM'
    if report.get('capture_failures'):raise AssertionError('Sunshine capture failed; see capture_failures and native topology checks')
    report['passed']=True
except BaseException as error:
    report['failure']=repr(error)
    debugger=os.environ.get('DESKPORT_TEST_GDB')
    if debugger:
        report['failure_processes']=subprocess.run(['ps','-eo','pid,comm,args'],capture_output=True,text=True).stdout
        for name in ('gnome-shell','deskport-display','deskport-host'):
            processes=subprocess.run(['pgrep','-f',f'(^|/){name}( |$)'],capture_output=True,text=True)
            for pid in processes.stdout.split():
                try:
                    executable=Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')[0].decode()
                    if name not in Path(executable).name:continue
                except (OSError,UnicodeError):continue
                try:
                    trace=subprocess.run(['sudo','-n',debugger,'-batch','-ex','set debuginfod enabled off',
                        '-ex','set pagination off','-ex','thread apply all bt','-p',pid],
                        capture_output=True,text=True,timeout=15)
                    report.setdefault('failure_thread_stacks',[]).append(dict(name=name,pid=pid,trace=trace.stdout+trace.stderr))
                except subprocess.TimeoutExpired as timeout:
                    report.setdefault('failure_thread_stacks',[]).append(dict(name=name,pid=pid,
                        trace=(timeout.stdout or b'').decode(errors='replace'),timed_out=True))
    raise
finally:
    for consumer in foreign_children:
        if consumer.poll() is None:consumer.terminate()
        consumer.wait(timeout=5)
    for p,log in children:
        if p.poll() is None:
            p.stdin.close()
            try:p.wait(timeout=20)
            except subprocess.TimeoutExpired:p.kill();p.wait(timeout=5)
        if p.stdin and not p.stdin.closed:p.stdin.close()
        if p.stdout:p.stdout.close()
        log.close()
    report['helper_logs']=[Path(log.name).read_text(errors='replace')[-12000:] for _,log in children]
    print(json.dumps(report,indent=2),flush=True)
    Path('result.json').write_text(json.dumps(report,indent=2)+'\n')
