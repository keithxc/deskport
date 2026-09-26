#!/usr/bin/env python3
"""Positive/negative controls for the opt-in glibc allocation observer."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile

if not sys.platform.startswith('linux'):
    raise SystemExit('This diagnostic test requires Linux/glibc')
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-buffer-watch-') as tmp:
    work = Path(tmp)
    cc = os.environ.get('CC', 'cc')
    library = work/'watch.so'
    subprocess.run([cc, '-std=gnu11', '-O2', '-shared', '-fPIC', '-pthread',
                    str(root/'scripts/diagnostics/pipewire-buffer-watch.c'),
                    '-o', str(library)], check=True)
    unit = work/'control.c'
    unit.write_text(r'''
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
static void *cycle(void *unused) {
    (void)unused;
    for (int i=0;i<1000;i++) {
        void *p=calloc(1,32768); assert(p);
        void *q=realloc(p,65536); assert(q); free(q);
        p=malloc(32768); assert(p); free(p);
    }
    return NULL;
}
int main(int argc,char **argv) {
    (void)argv;
    pthread_t threads[8];
    for(int i=0;i<8;i++)assert(pthread_create(&threads[i],NULL,cycle,NULL)==0);
    for(int i=0;i<8;i++)assert(pthread_join(threads[i],NULL)==0);
    void *held=malloc(32768);assert(held);
    if(argc>1)free(held);
}
''')
    binary = work/'control'
    subprocess.run([cc, '-O1', '-fno-builtin', '-pthread', str(unit), '-o', str(binary)], check=True)
    for clean in (False, True):
        log = work/('clean.log' if clean else 'retained.log')
        subprocess.run([str(binary)]+(['clean'] if clean else []), check=True,
                       env=dict(os.environ, LD_PRELOAD=str(library), BUFFER_WATCH_LOG=str(log)))
        result = subprocess.check_output(['python3', str(root/'scripts/diagnostics/summarize-pipewire-buffers.py'), str(log)], text=True)
        summary = json.loads(result)
        assert summary['complete'] and summary['tracked_allocations']==16001, summary
        assert summary['total_outstanding']==(0 if clean else 1), summary
    log.write_text('ALLOC 1 0x1234\n')
    assert subprocess.run(['python3', str(root/'scripts/diagnostics/summarize-pipewire-buffers.py'), str(log)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0
print('PASS 16001 concurrent allocation identities, realloc/free, retained-block detection, incomplete-log rejection')
