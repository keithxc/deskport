#!/usr/bin/env python3
"""Check generated Web resources, explicit UI boundaries and placeholder safety."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import importlib.util
import json
from html.parser import HTMLParser
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, str(ROOT/'scripts/sync-browser-translations.py'), '--check'], check=True)
spec = importlib.util.spec_from_file_location('core_ui', ROOT/'shared/deskport-core/i18n/catalog.py')
core = importlib.util.module_from_spec(spec)
spec.loader.exec_module(core)
rows = {k: v for k, v in core.records().items() if 'Browser' in v.get('_web', [])}
source = (ROOT/'app/browser/i18n-data.js').read_text()
data = json.loads(source.split('window.DeskPortUIResources = ', 1)[1].rstrip(';\n'))
assert data['context'] == 'Browser'
assert data['languages'] == [{k: v[k] for k in ('code', 'name', 'direction')} for v in core.languages()]
for language in data['languages']:
    code = language['code']
    assert set(data['strings'][code]) == set(rows), code
    for key, value in data['strings'][code].items():
        assert value and sorted(core.placeholders(key)) == sorted(core.placeholders(value)), (code, key)
        if key in data['fallbacks'][code]:
            assert value == key, (code, key)

class UI(HTMLParser):
    def __init__(self):
        super().__init__(); self.stack = []; self.ids = {}; self.keys = []
    def handle_starttag(self, tag, pairs):
        attrs = dict(pairs)
        if 'id' in attrs:
            assert attrs['id'] not in self.ids
            self.ids[attrs['id']] = attrs
        for name, value in pairs:
            if name.startswith('data-i18n'):
                assert value in rows, (tag, name, value)
                self.keys.append(value)
        if tag not in ('input', 'meta', 'link', 'img', 'br'):
            self.stack.append((tag, attrs))
    def handle_endtag(self, tag):
        if any(item[0] == tag for item in self.stack):
            while self.stack.pop()[0] != tag:
                pass
    def handle_data(self, value):
        if not value.strip() or not self.stack:
            return
        tag, attrs = self.stack[-1]
        # Keycaps, the product name, version and JS-disabled fallback are literals.
        if tag == 'noscript' or 'data-keys' in attrs or value.strip() in ('DeskPort', 'v@DESKPORT_VERSION@', '↗', '⋯'):
            return
        assert 'data-i18n' in attrs, (tag, value)
ui = UI(); ui.feed((ROOT/'app/browser/index.html').read_text())
assert ui.ids['access-code']['dir'] == 'ltr'
for identifier in ('host-name', 'paired-name', 'device-name', 'remote-text'):
    assert ui.ids[identifier]['dir'] == 'auto', identifier
assert ui.ids['quality'] and ui.ids['display-mode'] and ui.ids['touch-mode']
js = (ROOT/'app/browser/app.js').read_text()
assert not re.search(r'[\u4e00-\u9fff]', js)
# Every authored sentence in the application is registered. Machine values,
# comments, endpoints and arbitrary server/user text are deliberately excluded.
for value in re.findall(r'"([^"\n]+)"', js):
    if ' ' in value and not value.startswith(('/', 'application/')):
        assert value in rows, value
for path in ('i18n.js', 'i18n-data.js'):
    assert f'<file>{path}</file>' in (ROOT/'app/browser/browser.qrc').read_text()
    assert f'"/{path}"' in (ROOT/'app/backend/browsergateway.cpp').read_text()
for identifier in ('displayPolicy', 'balanced', 'smooth', 'sharp', 'trackpad', 'direct', 'taken-over', 'unpaired'):
    assert identifier not in rows, identifier
print(f'PASS: {len(ui.keys)} explicit HTML boundaries; {len(rows)} Web originals, 22 complete resources, no protocol localization')
print('English fallbacks: '+json.dumps({code: len(keys) for code, keys in data['fallbacks'].items()}, sort_keys=True))
