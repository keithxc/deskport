#!/usr/bin/env python3
"""Software-only cancellation/ownership checks; never creates a VT session."""
import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('saver_regression', ROOT / 'tests/test-saver-regression.py')
DRIVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRIVER)

FAKE = '''import json,os,signal,subprocess,sys,time
from pathlib import Path
signal.signal(signal.SIGTERM,signal.SIG_IGN)
if len(sys.argv)>2:
    child=subprocess.Popen([sys.executable,__file__,sys.argv[1]])
    Path(sys.argv[1]+'.group').write_text(str(os.getpgrp()))
else:
    Path(sys.argv[1]).write_text(str(os.getpid()))
time.sleep(60)
'''


class ProcessOwnership(unittest.TestCase):
    def setUp(self):
        checkout = subprocess.check_output(['bash', str(ROOT / 'scripts/build-paths.sh'), 'checkout'], text=True).strip()
        base = Path(os.environ.get('DESKPORT_TEST_DIR', Path(checkout) / 'tests'))
        base.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='saver-control-', dir=base)
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.fake = self.work / 'fake.py'
        self.fake.write_text(FAKE)
        self.ready = self.work / 'ready'

    def wait_file(self, path, child=None):
        deadline = time.monotonic() + 10
        while not path.exists():
            if child is not None and child.poll() is not None:
                self.fail('controller exited before its fake phase became ready')
            if time.monotonic() > deadline:
                self.fail(f'fake phase did not write {path}')
            time.sleep(0.02)

    def assert_group_gone(self):
        self.wait_file(Path(str(self.ready) + '.group'))
        group = int(Path(str(self.ready) + '.group').read_text())
        self.assertFalse(DRIVER.group_exists(group), f'owned process group {group} survived cleanup')

    def test_timeout_kills_phase_and_descendant(self):
        with self.assertRaises(subprocess.TimeoutExpired):
            DRIVER.run_phase('timeout', [sys.executable, str(self.fake), str(self.ready), 'parent'],
                             logs=self.work, env=os.environ.copy(), timeout=1, cleanup_grace=0.3)
        self.assert_group_gone()

    def test_sigterm_cleans_phase_before_unlock(self):
        controller = self.work / 'controller.py'
        controller.write_text('''import importlib.util,os,sys
from pathlib import Path
p=Path(sys.argv[1]);s=importlib.util.spec_from_file_location('driver',p)
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
work=Path(sys.argv[2])
with m.owned_replay(work),m.cleanup_signals():
 m.run_phase('signal',[sys.executable,str(work/'fake.py'),str(work/'ready'),'parent'],logs=work,env=os.environ.copy(),timeout=30,cleanup_grace=0.3)
''')
        environment = dict(os.environ, DESKPORT_BUILD_ROOT=str(self.work / 'project'), PYTHONDONTWRITEBYTECODE='1')
        child = subprocess.Popen([sys.executable, str(controller), str(ROOT / 'tests/test-saver-regression.py'), str(self.work)], env=environment)
        try:
            self.wait_file(self.ready, child)
            child.send_signal(signal.SIGTERM)
            self.assertEqual(child.wait(timeout=10), 128 + signal.SIGTERM)
            self.assert_group_gone()
            self.assertEqual((self.work / 'project/locks/native-replay.lock').read_text(), '')
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()
            group_file = Path(str(self.ready) + '.group')
            if group_file.exists():
                try:
                    os.killpg(int(group_file.read_text()), signal.SIGKILL)
                except ProcessLookupError:
                    pass


if __name__ == '__main__':
    unittest.main()
