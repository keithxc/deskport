#!/usr/bin/env python3
"""Render the shared offline manual into the GitHub Pages site.

The manual stays single-sourced in shared/deskport-core/manual/manual.json.
For every site language this writes manual.html, using that language's
support.html as the page shell (head, navigation, language switcher, footer),
and adds a Manual link to the navigation of the site's other pages.

It also owns the per-language plumbing of every page: html lang/dir,
canonical and og:url, og:locale alternates, hreflang alternates, the language
switcher and the root auto-detect script, all generated from SITE_LANGUAGES.
"""
import argparse
import html
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'shared/deskport-core'
BASE_URL = 'https://keithxc.github.io/deskport/'
# Site directory ('' is English at the root) -> (manual code, og:locale, native name).
LANGUAGE_INFO = {
    '': ('en', 'en_US', 'English'),
    'zh-CN': ('zh-Hans', 'zh_CN', '简体中文'),
    'zh-TW': ('zh-Hant', 'zh_TW', '繁體中文'),
    'ja': ('ja', 'ja_JP', '日本語'),
    'ko': ('ko', 'ko_KR', '한국어'),
    'de': ('de', 'de_DE', 'Deutsch'),
    'es': ('es', 'es_ES', 'Español'),
    'fr': ('fr', 'fr_FR', 'Français'),
    'pt-BR': ('pt', 'pt_BR', 'Português (Brasil)'),
    'ru': ('ru', 'ru_RU', 'Русский'),
    'it': ('it', 'it_IT', 'Italiano'),
    'nl': ('nl', 'nl_NL', 'Nederlands'),
    'pl': ('pl', 'pl_PL', 'Polski'),
    'uk': ('uk', 'uk_UA', 'Українська'),
    'tr': ('tr', 'tr_TR', 'Türkçe'),
    'ar': ('ar', 'ar_AR', 'العربية'),
    'hi': ('hi', 'hi_IN', 'हिन्दी'),
    'th': ('th', 'th_TH', 'ไทย'),
    'vi': ('vi', 'vi_VN', 'Tiếng Việt'),
    'id': ('id', 'id_ID', 'Bahasa Indonesia'),
    'ms': ('ms', 'ms_MY', 'Bahasa Melayu'),
    'bo': ('bo', 'bo_CN', 'བོད་ཡིག'),
}
SITE_LANGUAGES = {directory: info[0] for directory, info in LANGUAGE_INFO.items()}
RTL = {'ar'}
PAGES = ('index.html', 'support.html', 'privacy.html', 'source.html')
SUPPORT_LINK = re.compile(r'<a href="support\.html"[^>]*>[^<]*</a>')


def source_language(entry, language):
    """Match app/gui/manual.cpp: exact, base language, sibling variant, English."""
    if language in entry:
        return language
    base = language.split('-')[0]
    if base in entry:
        return base
    return next((key for key in sorted(entry) if key.startswith(base)), 'en')


def localized(entry, language):
    return entry[source_language(entry, language)]


def item(entry, language):
    """List item text, marked as English when the translation is still missing."""
    text = html.escape(localized(entry, language))
    fallback = source_language(entry, language) == 'en' != language
    return f'<li lang="en">{text}</li>' if fallback else f'<li>{text}</li>'


def manual_link(label, current=False):
    attribute = ' aria-current="page"' if current else ''
    return f'<a href="manual.html"{attribute}>{html.escape(label)}</a>'


def add_nav_link(page, label, current=False):
    """Insert the Manual link after Setup & Support once; idempotent."""
    nav = re.search(r'<nav[^>]*>.*?</nav>', page, re.S)
    assert nav, 'site page has no main navigation'
    links = re.sub(r'<a href="manual\.html"[^>]*>[^<]*</a>', '', nav.group(0))
    links, count = SUPPORT_LINK.subn(lambda match: match.group(0) + manual_link(label, current),
                                     links, count=1)
    assert count == 1, 'site navigation has no Setup & Support link'
    return page[:nav.start()] + links + page[nav.end():]


def body(manual, language, label):
    chapters = manual['chapters']
    lines = [f'<h1>{html.escape(label)}</h1>',
             '<ol class="manual-toc">' + ''.join(
                 f'<li><a href="#{chapter["id"]}">{html.escape(localized(chapter["title"], language))}</a></li>'
                 for chapter in chapters) + '</ol>']
    for chapter in chapters:
        steps = ''.join(item(step['text'], language) for step in chapter['steps'])
        lines.append(f'<section id="{chapter["id"]}"><h2>'
                     f'{html.escape(localized(chapter["title"], language))}</h2><ol>{steps}</ol></section>')
    return '\n'.join(lines)


def render(shell, manual, language, label):
    """Turn a support.html shell into manual.html for the same language."""
    title = f'{label} | DeskPort'
    description = html.escape(localized(manual['chapters'][0]['steps'][0]['text'], language), quote=True)
    nav = re.search(r'<nav[^>]*>.*?</nav>', shell, re.S)
    head, tail = shell[:nav.start()], shell[nav.end():]
    # Head and language switcher point at this page; the nav keeps its Setup & Support link.
    head = head.replace('support.html', 'manual.html')
    head = re.sub(r'<title>[^<]*</title>', f'<title>{html.escape(title)}</title>', head)
    head = re.sub(r'(<meta property="og:title" content=")[^"]*', rf'\g<1>{html.escape(title, quote=True)}', head)
    for name in ('name="description"', 'property="og:description"'):
        head = re.sub(rf'(<meta {name} content=")[^"]*', rf'\g<1>{description}', head)
    links, switcher = nav.group(0).split('<details', 1)
    links = add_nav_link(links + '</nav>', label, current=True)[:-len('</nav>')]
    switcher = switcher.replace('support.html', 'manual.html')
    main = re.search(r'<main>.*?</main>', tail, re.S)
    assert main, 'site page has no main element'
    footer = re.sub(r'(<footer>DeskPort) · [^·<]+ ·', r'\1 ·', tail[main.end():])
    return (head + links + '<details' + switcher + tail[:main.start()]
            + f'<main>\n{body(manual, language, label)}\n</main>' + footer)


def hreflang(directory):
    return directory or 'en'


def page_url(directory, name):
    folder = f'{directory}/' if directory else ''
    return BASE_URL + folder + ('' if name == 'index.html' else name)


def relative(source, target, name):
    """Link from a page in `source` to `name` in `target`, as the site writes them."""
    if source == target:
        return name
    return ('../' if source else '') + (f'{target}/' if target else '') + name


AUTO_DETECT = (
    "<script>(function(){try{var q=location.search;if(/[?&]lang=/.test(q)||"
    "localStorage.getItem('deskport-lang')||/bot|crawl|spider|slurp/i.test(navigator.userAgent))return;"
    "var S=%s,L=navigator.languages&&navigator.languages.length?navigator.languages:"
    "[navigator.language||''];for(var i=0;i<L.length;i++){var l=String(L[i]).toLowerCase(),"
    "b=l.split('-')[0],t=null;if(b==='en')return;if(b==='zh')t=/hant|-tw|-hk|-mo/.test(l)?'zh-TW':'zh-CN';"
    "else if(b==='pt')t='pt-BR';else if(S.indexOf(b)>=0)t=b;if(t){location.replace(t+'/'+"
    "(location.pathname.split('/').pop()||'')+location.hash);return}}}catch(e){}})();</script>")


def switch_item(directory, other, name):
    code = hreflang(other)
    current = ' aria-current="true"' if other == directory else ''
    remember = f"try{{localStorage.setItem('deskport-lang','{code}')}}catch(e){{}}"
    return (f'<li><a href="{relative(directory, other, name)}" hreflang="{code}" lang="{code}"'
            f'{current} onclick="{remember}">{LANGUAGE_INFO[other][2]}</a></li>')


def sync_languages(page, directory, name):
    """Regenerate the language plumbing of one page from LANGUAGE_INFO."""
    direction = ' dir="rtl"' if directory in RTL else ''
    page = re.sub(r'<html[^>]*>', f'<html lang="{hreflang(directory)}"{direction}>', page, count=1)
    url = page_url(directory, name)
    page = re.sub(r'(<meta property="og:url" content=")[^"]*', rf'\g<1>{url}', page, count=1)
    page = re.sub(r'(<link rel="canonical" href=")[^"]*', rf'\g<1>{url}', page, count=1)
    page = re.sub(r'<meta property="og:locale:alternate" content="[^"]*">', '', page)
    others = ''.join(f'<meta property="og:locale:alternate" content="{info[1]}">'
                     for other, info in LANGUAGE_INFO.items() if other != directory)
    page = re.sub(r'(<meta property="og:locale" content=")[^"]*(">)',
                  lambda match: match.group(1) + LANGUAGE_INFO[directory][1] + match.group(2) + others,
                  page, count=1)
    page = re.sub(r'<link rel="alternate" hreflang="[^"]*" href="[^"]*">', '', page)
    alternates = ''.join(f'<link rel="alternate" hreflang="{hreflang(other)}" href="{page_url(other, name)}">'
                         for other in LANGUAGE_INFO)
    alternates += f'<link rel="alternate" hreflang="x-default" href="{page_url("", name)}">'
    page = re.sub(r'<link rel="canonical" href="[^"]*">', lambda match: match.group(0) + alternates,
                  page, count=1)
    detected = '[' + ','.join(f"'{other}'" for other in LANGUAGE_INFO if other) + ']'
    page = re.sub(r'<script>\(function\(\)\{try\{var q=location\.search.*?</script>',
                  lambda match: AUTO_DETECT % detected, page, count=1, flags=re.S)
    items = ''.join(switch_item(directory, other, name) for other in LANGUAGE_INFO)
    page = re.sub(r'(<details class="lang-switch">.*?<span>)[^<]*(</span></summary>)<ul>.*?</ul>',
                  lambda match: match.group(1) + LANGUAGE_INFO[directory][2] + match.group(2)
                  + f'<ul>{items}</ul>', page, count=1, flags=re.S)
    return page


def resolve_site(path):
    site = Path(path).expanduser().resolve()
    if site == ROOT or ROOT in site.parents:
        raise SystemExit('Use a gh-pages worktree outside the source checkout.')
    for directory in SITE_LANGUAGES:
        if not (site / directory / 'support.html').is_file():
            raise SystemExit(f'{site / directory / "support.html"} is missing; is this the gh-pages tree?')
    return site


def export(site):
    manual = json.loads((CORE / 'manual/manual.json').read_text(encoding='utf-8'))
    strings = json.loads((CORE / 'i18n/strings.json').read_text(encoding='utf-8'))
    written = []
    for directory, language in SITE_LANGUAGES.items():
        label = localized({key: value for key, value in strings['Manual'].items()
                           if not key.startswith('_')}, language)
        folder = site / directory
        for name in PAGES:
            page = folder / name
            if page.is_file():
                text = add_nav_link(page.read_text(encoding='utf-8'), label)
                page.write_text(sync_languages(text, directory, name), encoding='utf-8')
        shell = (folder / 'support.html').read_text(encoding='utf-8')
        target = folder / 'manual.html'
        text = render(shell, manual, language, label)
        target.write_text(sync_languages(text, directory, 'manual.html'), encoding='utf-8')
        written.append(target)
    return written


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--site', required=True, help='gh-pages worktree outside this checkout')
    args = parser.parse_args()
    written = export(resolve_site(args.site))
    print(f'PASS: wrote {len(written)} manual pages')


if __name__ == '__main__':
    main()
