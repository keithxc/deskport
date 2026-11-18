#!/usr/bin/env python3
"""Native regression: a pre-existing consumer reads a newly admitted display mode."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import argparse,json,os,select,subprocess,tempfile
from pathlib import Path
parser=argparse.ArgumentParser(); parser.add_argument('helper'); parser.add_argument('--native',action='store_true'); args=parser.parse_args()
if not args.native: parser.error('--native is required for a temporary extended display')
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-mode-cache-') as tmp:
    work=Path(tmp); descriptor=work/'capture-display'; source=work/'observer.c'
    source.write_text(r'''
#include "admitted-display.h"
int main(void) {
    CGDirectDisplayID ids[128]; unsigned count;
    CGGetOnlineDisplayList(128,ids,&count);
    for (unsigned i=0;i<count;i++) { CGDisplayModeRef m=CGDisplayCopyDisplayMode(ids[i]); if(m)CFRelease(m); }
    puts("ready"); fflush(stdout);
    unsigned id;
    while(scanf("%u",&id)==1) {
        int w=0,h=0,s=0; int ok=dp_admitted_display_mode(id,&w,&h,&s);
        printf("%d %d %d %d\n",ok,w,h,s); fflush(stdout);
    }
}
''')
    subprocess.run(['xcrun','clang',str(source),'-I'+str(root/'host/macos'),'-framework','CoreGraphics','-framework','CoreFoundation','-o',str(work/'observer')],check=True)
    observer=subprocess.Popen([str(work/'observer')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,env=dict(os.environ,DESKPORT_CAPTURE_DISPLAY_FILE=str(descriptor)))
    assert observer.stdout.readline().strip()=='ready'
    helper=subprocess.Popen([str(Path(args.helper).resolve()),'2560','1440'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,env=dict(os.environ,DESKPORT_DISPLAY_ISOLATED='1',DESKPORT_DISPLAY_STATE_DIR=tmp,DESKPORT_DISPLAY_SERIAL='2147483912'))
    def receive(seq):
        while True:
            assert select.select([helper.stdout],[],[],15)[0], 'helper timed out'
            line=helper.stdout.readline(); assert line, 'helper exited'
            result=json.loads(line)
            if result.get('seq')==seq:
                assert 'error' not in result,result
                return result
    def query(ident):
        observer.stdin.write(str(ident)+'\n');observer.stdin.flush()
        return tuple(map(int,observer.stdout.readline().split()))
    try:
        assert json.loads(helper.stdout.readline())==dict(ready=True,active=False)
        for cycle,(w,h) in enumerate([(3824,2000),(2560,1440)],1):
            seq=cycle*2
            helper.stdin.write(json.dumps(dict(seq=seq,width=w,height=h,scale=2,session=True,displayPolicy=2))+'\n');helper.stdin.flush()
            r=receive(seq); ident=r['displayId']
            assert query(ident)[0]==0, 'Absent descriptor was accepted'
            descriptor.write_text(f'{ident} {w} {h} 2\n');assert query(ident)==(1,w,h,2)
            descriptor.write_text(f'{ident} {w+400} {h} 2\n');assert query(ident)[0]==0, 'Mismatched live bounds were accepted'
            descriptor.write_text(f'{ident} {w} {h} 2\n')
            helper.stdin.write(json.dumps(dict(seq=seq+1,width=w,height=h,scale=2,session=False,displayPolicy=2))+'\n');helper.stdin.flush();receive(seq+1)
            assert query(ident)[0]==0, 'Removed display was accepted'
            descriptor.unlink()
            print(f'PASS: persistent consumer, {w}x{h}@2, missing/mismatched/removed descriptor rejection',flush=True)
    finally:
        helper.stdin.close();helper.wait(timeout=15)
        observer.stdin.close();observer.wait(timeout=5)
