#!/usr/bin/env python3
"""Production builds must ignore the diagnostics environment variable entirely."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-memory-macro-') as tmp:
    p=Path(tmp)
    (p/'test.cpp').write_text('''#include "host/common/memorydiagnostics.h"
int main() { deskport_memory::lifetime owner(deskport_memory::kind::image); deskport_memory::snapshot("test"); }
''')
    for enabled in (0,1):
        binary=p/('test-'+str(enabled));log=p/('memory-'+str(enabled)+'.jsonl')
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O2','-pthread','-I'+str(root),'-DDESKPORT_ENABLE_MEMORY_DIAGNOSTICS='+str(enabled),str(p/'test.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],env=dict(os.environ,DESKPORT_MEMORY_DIAGNOSTICS=str(log)),check=True)
        assert log.exists()==bool(enabled)
        if not enabled:
            assert b'DESKPORT_MEMORY_DIAGNOSTICS' not in binary.read_bytes()
    print('PASS production removes diagnostics; explicit diagnostic build writes counters')
