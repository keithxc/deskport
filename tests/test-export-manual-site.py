#!/usr/bin/env python3
"""Verify the Pages manual export against a disposable copy of the site layout."""
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('export_manual_site', ROOT / 'scripts/export-manual-site.py')
site_export = importlib.util.module_from_spec(spec)
spec.loader.exec_module(site_export)
base = Path(subprocess.check_output(['bash', str(ROOT / 'scripts/build-paths.sh'), 'checkout'],
                                    text=True).strip()) / 'manual-site-tests.noindex'
base.mkdir(parents=True, exist_ok=True)
manual = json.loads((ROOT / 'shared/deskport-core/manual/manual.json').read_text(encoding='utf-8'))
checks = 0


def check(condition, message):
    global checks
    if not condition:
        raise AssertionError(message)
    checks += 1


def page(directory, name):
    prefix = '../' if directory else ''
    switch = ''.join(f'<li><a href="{prefix}{d + "/" if d else ""}{name}">{d or "en"}</a></li>'
                     for d in site_export.SITE_LANGUAGES)
    script = ("<script>(function(){try{var q=location.search;var S=['ja'];}catch(e){}})();</script>"
              if not directory else '')
    return ('<!doctype html><html lang="xx"><head><meta name="description" content="Old">'
            '<meta property="og:title" content="Setup &amp; Support | DeskPort">'
            '<meta property="og:url" content="https://example.test/old">'
            '<meta property="og:locale" content="xx_XX"><meta property="og:locale:alternate" content="yy_YY">'
            '<link rel="canonical" href="https://example.test/old">'
            '<link rel="alternate" hreflang="yy" href="https://example.test/yy">'
            f'{script}<title>Setup &amp; Support | DeskPort</title></head><body>'
            '<nav aria-label="Main"><a href="index.html">DeskPort</a>'
            '<a href="support.html">Setup &amp; Support</a><a href="privacy.html">Privacy</a>'
            f'<details class="lang-switch"><summary aria-label="Language"><span>x</span></summary><ul>{switch}</ul></details></nav>'
            '<main><h1>Support</h1></main><footer>DeskPort · Updated October 12, 2026 · '
            '<a href="https://github.com/keithxc/deskport">GitHub</a></footer></body></html>')


with tempfile.TemporaryDirectory(prefix='site-', dir=base) as temporary:
    site = Path(temporary)
    for directory in site_export.SITE_LANGUAGES:
        (site / directory).mkdir(exist_ok=True)
        for name in site_export.PAGES:
            (site / directory / name).write_text(page(directory, name), encoding='utf-8')
    site_export.export(site_export.resolve_site(site))
    first = {path: path.read_text(encoding='utf-8') for path in site.rglob('*.html')}
    site_export.export(site)
    check(first == {path: path.read_text(encoding='utf-8') for path in site.rglob('*.html')},
          'export is not idempotent')
    for directory, language in site_export.SITE_LANGUAGES.items():
        text = (site / directory / 'manual.html').read_text(encoding='utf-8')
        nav = re.search(r'<nav.*?<details', text, re.S).group(0)
        check(nav.count('href="manual.html"') == 1 and 'aria-current="page"' in nav,
              f'{directory}: manual nav link')
        check('href="support.html"' in nav, f'{directory}: support link kept')
        check('support.html' not in text.split('<nav')[0], f'{directory}: head still names support')
        check('support.html' not in text.split('<details')[1].split('</details>')[0],
              f'{directory}: language switcher still names support')
        check('Updated' not in text and '<h1>Support</h1>' not in text, f'{directory}: stale shell')
        for chapter in manual['chapters']:
            check(f'<section id="{chapter["id"]}">' in text and f'href="#{chapter["id"]}"' in text,
                  f'{directory}: chapter {chapter["id"]}')
        check(text.count('<li lang="en">') == (0 if language == 'en' else sum(
            site_export.source_language(step['text'], language) == 'en'
            for chapter in manual['chapters'] for step in chapter['steps'])),
              f'{directory}: English fallback marking')
        url = site_export.page_url(directory, 'manual.html')
        check(f'<link rel="canonical" href="{url}">' in text and f'content="{url}"' in text,
              f'{directory}: canonical and og:url')
        check(text.count('rel="alternate" hreflang=') == len(site_export.LANGUAGE_INFO) + 1
              and 'hreflang="yy"' not in text, f'{directory}: hreflang alternates')
        check(text.count('og:locale:alternate') == len(site_export.LANGUAGE_INFO) - 1,
              f'{directory}: og:locale alternates')
        own = directory or 'en'
        rtl = ' dir="rtl"' if directory in site_export.RTL else ''
        check(f'<html lang="{own}"{rtl}>' in text, f'{directory}: html lang and direction')
        switch = text.split('<ul>')[-1].split('</ul>')[0]
        check(switch.count('<li>') == len(site_export.LANGUAGE_INFO) and switch.count('aria-current') == 1
              and f'lang="{own}" aria-current="true"' in switch, f'{directory}: language switcher')
        if not directory:
            root = (site / 'index.html').read_text(encoding='utf-8')
            check("'bo'" in root and "var S=['ja']" not in root and 'location.pathname' in root,
                  'root auto-detect covers every language')
        for name in site_export.PAGES:
            other = (site / directory / name).read_text(encoding='utf-8')
            check(other.count('href="manual.html"') == 1 and 'aria-current="page"' not in other,
                  f'{directory}/{name}: manual nav link')
    try:
        site_export.resolve_site(ROOT / 'docs')
    except SystemExit:
        checks += 1
    else:
        raise AssertionError('accepted a site inside the checkout')

print(f'PASS manual site export: {checks} checks')
