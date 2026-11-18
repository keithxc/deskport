#!/usr/bin/env python3
"""Link an isolated manager/model regression against a built macOS desktop app.

Usage: nix develop -c python3 tests/test-computer-removal.py BUILD_ROOT/app
Build the desktop first. This reuses real production objects without replacing
main.o or touching the built application. No hosts are polled or connected.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import os
from pathlib import Path
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[1]
build=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='deskport-removal-') as temp:
    work=Path(temp)
    subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-subj','/CN=isolated-fixture','-keyout',str(work/'key.pem'),'-out',str(work/'cert.pem')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    (work/'test.mk').write_text(f'''include {build}/Makefile.Release
.PHONY: removal-regression
removal-regression:
\t$(CXX) -c $(CXXFLAGS) $(INCPATH) "{root}/tests/computer-removal.cpp" -o "{work}/test.o"
\t$(LINK) $(LFLAGS) -o "{work}/test" "{work}/test.o" $(filter-out release/main.o,$(OBJECTS)) $(LIBS)
''')
    subprocess.run(['make','-f',str(work/'test.mk'),'removal-regression'],cwd=build,check=True,stdout=subprocess.DEVNULL)
    subprocess.run([str(work/'test')],check=True,timeout=60,env={**os.environ,'DYLD_FRAMEWORK_PATH':str(build/'DeskPort.app/Contents/Frameworks'),'DYLD_LIBRARY_PATH':str(build/'DeskPort.app/Contents/Frameworks'),'QT_QPA_PLATFORM':'offscreen','TEST_CERT_A':str(work/'cert.pem')})
