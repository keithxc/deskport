#!/usr/bin/env python3
"""Real WebKitGTK WebDriver pairing probe on an already isolated Xvfb fixture.

This proves WebKitGTK engine behavior, not physical iPad/Safari acceptance.
It uses the production HTTPS API/frontend and MiniBrowser's private test cookie
database. Never run against a personal host or a personal browser profile.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import time
import urllib.error
import urllib.request

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fixture',type=Path,required=True)
parser.add_argument('--webdriver',type=Path,required=True)
args=parser.parse_args(); fixture=json.loads(args.fixture.read_text())
output=Path(fixture['outputDir'])/'webkit'; output.mkdir(parents=True,exist_ok=True)
checks=[]; session=None; driver=None
def check(name,condition=True):
    if not condition: raise AssertionError(name)
    checks.append(name)
def control(action,**extra):
    with socket.socket(socket.AF_UNIX) as connection:
        connection.settimeout(25); connection.connect(fixture['controlSocket'])
        connection.sendall(json.dumps({'action':action,**extra}).encode()+b'\n')
        data=b''
        while b'\n' not in data:data+=connection.recv(8192)
        value=json.loads(data.split(b'\n')[0]); check('local fixture '+action,value.get('ok')); return value
with socket.socket() as probe:
    probe.bind(('127.0.0.1',0)); port=probe.getsockname()[1]
endpoint=f'http://127.0.0.1:{port}'
def request(method,path,body=None):
    data=None if body is None else json.dumps(body).encode()
    req=urllib.request.Request(endpoint+path,data=data,method=method,headers={'Content-Type':'application/json'})
    try:
        with urllib.request.urlopen(req,timeout=30) as response: result=json.load(response)['value']
    except urllib.error.HTTPError as error:
        # WebDriver diagnostics only; never record command arguments or cookies.
        result=json.loads(error.read()).get('value',{})
        raise RuntimeError('WebDriver '+str(error.code)+': '+str(result.get('message','request failed'))[:256]) from None
    if isinstance(result,dict) and 'error' in result:raise RuntimeError(result.get('message','WebDriver failed'))
    return result
def command(method,path,body=None):return request(method,'/session/'+session+path,body)
def execute(script,*values):return command('POST','/execute/sync',{'script':script,'args':list(values)})
def fetch(path,body):
    return command('POST','/execute/async',{'script':'''const done=arguments[arguments.length-1];
fetch(arguments[0],{method:'POST',credentials:'same-origin',headers:{'Content-Type':'application/json'},body:JSON.stringify(arguments[1])})
.then(async response=>done({status:response.status,data:await response.json()})).catch(error=>done({error:error.message}));''',
        'args':[path,body]})
def new_session():
    global session
    browser=args.webdriver.parent.parent/'libexec/webkit2gtk-4.1/MiniBrowser'
    value=request('POST','/session',{'capabilities':{'alwaysMatch':{'browserName':'MiniBrowser','acceptInsecureCerts':True,
        'webkitgtk:browserOptions':{'binary':str(browser),'args':['--automation',
            '--cookies-file='+str(output/'cookies.sqlite'),'--ignore-tls-errors']}}}})
    session=value['sessionId']; command('POST','/timeouts',{'script':20000,'pageLoad':30000,'implicit':0})
    command('POST','/url',{'url':fixture['url']}); return value.get('capabilities',{})
def ready(paired):
    deadline=time.monotonic()+20
    while time.monotonic()<deadline:
        if execute("return document.body.dataset.paired===String(arguments[0]) && document.getElementById('connection-status')?.dataset.state==='idle'",paired):return
        time.sleep(.1)
    raise TimeoutError('WebKit frontend pairing state did not settle')
result={}
try:
    log=(output/'webdriver.log').open('wb')
    driver=subprocess.Popen([str(args.webdriver),'--host=127.0.0.1','--port='+str(port)],stdout=log,stderr=log,
        env=dict(os.environ,WEBKIT_DISABLE_DMABUF_RENDERER='1',LIBGL_ALWAYS_SOFTWARE='1',GDK_BACKEND='x11'),
        start_new_session=True)
    for _ in range(100):
        try:request('GET','/status');break
        except OSError:time.sleep(.1)
    capabilities=new_session(); ready(False)
    check('WebKit production page is secure',execute('return isSecureContext'))
    # Each code works once; take the fixture's next 30-second code.
    login=fetch('/api/login',{'code':control('next-code')['code'],'remember':True,'deviceName':'WebKit fixture'})
    check('WebKit real HTTPS persistent login',login.get('status')==200 and login['data'].get('paired'))
    pair_id=login['data']['pairing']['id']
    cookies=command('GET','/cookie')
    cookie=next(row for row in cookies if row['name']=='__Host-deskport-pairing')
    remaining=cookie.get('expiry',0)-time.time()
    check('WebKit stores Secure HttpOnly seven-day cookie',cookie.get('secure') and cookie.get('httpOnly') and 6*86400<remaining<8*86400)
    check('WebKit script cannot read auth cookies',execute("return !document.cookie.includes('deskport') && localStorage.length===0 && sessionStorage.length===0"))
    command('POST','/refresh',{});ready(True)
    check('WebKit frontend resume does not stream',execute("return document.getElementById('desktop-video').srcObject===null"))
    command('DELETE','');session=None
    new_session();ready(True)
    check('WebKit browser process restart preserves pairing',True)
    control('restart');command('POST','/refresh',{});ready(True)
    check('WebKit gateway restart preserves pairing',True)
    control('revoke',id=pair_id);command('POST','/refresh',{});ready(False)
    check('WebKit host revocation clears paired UI',True)
    denied=fetch('/api/resume',{})
    check('WebKit revoked cookie cannot authorize',denied.get('status')==401 and denied['data'].get('code')=='unpaired')
    result={'ok':True,'checks':checks,'capabilities':capabilities,
        'certificateValidation':'Legacy isolated automation bypass; not certificate-trust acceptance',
        'scope':'WebKitGTK MiniBrowser on isolated Xvfb with a private persistent cookie database and real production HTTPS/frontend. No Safari/iPad or native-media claim.'}
except Exception as error:
    result={'ok':False,'error':str(error),'checks':checks,'scope':'WebKitGTK attempt only; not physical Safari/iPad'}
finally:
    if session:
        try:command('DELETE','')
        except Exception:pass
    if driver and driver.poll() is None:
        os.killpg(driver.pid,signal.SIGTERM)
        try:driver.wait(timeout=10)
        except subprocess.TimeoutExpired:os.killpg(driver.pid,signal.SIGKILL);driver.wait(timeout=5)
    if 'log' in globals():log.close()
    (output/'webkit-result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'ok':result.get('ok'),'checks':len(checks),'error':result.get('error'),'output':str(output/'webkit-result.json')}))
raise SystemExit(0 if result.get('ok') else 1)
