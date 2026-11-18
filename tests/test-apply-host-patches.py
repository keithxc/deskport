#!/usr/bin/env python3
"""Check host/series parsing, platform selection and the runner's actions."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('apply_host_patches', ROOT / 'scripts/apply-host-patches.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class SeriesTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.repo = Path(self.temp.name) / 'repo'
        self.source = Path(self.temp.name) / 'source'
        (self.repo / 'host').mkdir(parents=True)
        (self.repo / 'host/overlays').mkdir()
        self.source.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def write_series(self, text):
        path = self.repo / 'host/series'
        path.write_text(text)
        return runner.parse_series(path)

    def test_platform_tags_select_linux_variants(self):
        (self.repo / 'host/a.h').write_text('a\n')
        entries = self.write_series(
            '# comment\n'
            'all            copy  host/a.h  one/\n'
            'linux          copy  host/a.h  two/\n'
            'linux-nix      copy  host/a.h  three/\n'
            'macos,windows  copy  host/a.h  four/\n')
        def lines(platform):
            return [number for number, *_ in runner.plan(entries, platform, self.repo, self.source)]
        self.assertEqual(lines('linux-nix'), [2, 3, 4])
        self.assertEqual(lines('linux-portable'), [2, 3])
        self.assertEqual(lines('windows'), [2, 5])

    def test_rejects_unknown_platform_action_and_missing_input(self):
        with self.assertRaises(SystemExit):
            self.write_series('linux-arm  copy  a  b\n')
        with self.assertRaises(SystemExit):
            self.write_series('all  remove  a  b\n')
        entries = self.write_series('all  script  missing.py  {src}\n')
        with self.assertRaises(SystemExit):
            runner.plan(entries, 'macos', self.repo, self.source)

    def test_runs_copy_script_and_apply_in_order(self):
        (self.repo / 'host/a.h').write_text('header\n')
        (self.repo / 'host/overlays/append.py').write_text(
            'import sys, pathlib\np = pathlib.Path(sys.argv[1]) / "a.txt"\np.write_text(p.read_text() + "script\\n")\n')
        (self.source / 'a.txt').write_text('base\n')
        (self.repo / 'host/a.patch').write_text(
            '--- a/a.txt\n+++ b/a.txt\n@@ -1,2 +1,3 @@\n base\n script\n+patch\n')
        entries = self.write_series(
            'all  copy    host/a.h  inc/\n'
            'all  copy    host/a.h  other/b.h\n'
            'all  script  append.py  {src}\n'
            'all  apply   .  host/a.patch\n')
        runner.run(runner.plan(entries, 'macos', self.repo, self.source), self.repo, self.source)
        self.assertEqual((self.source / 'inc/a.h').read_text(), 'header\n')
        self.assertEqual((self.source / 'other/b.h').read_text(), 'header\n')
        self.assertEqual((self.source / 'a.txt').read_text(), 'base\nscript\npatch\n')

    def test_repository_series_plans_every_platform(self):
        entries = runner.parse_series(ROOT / 'host/series')
        for platform in sorted(runner.PLATFORMS):
            self.assertTrue(runner.plan(entries, platform, ROOT, self.source), platform)


if __name__ == '__main__':
    unittest.main()
