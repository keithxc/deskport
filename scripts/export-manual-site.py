#!/usr/bin/env python3
"""Render the shared offline manual into the GitHub Pages site.

The manual stays single-sourced in shared/deskport-core/manual/manual.json.
For every site language this writes manual.html, using that language's
support.html as the page shell (head, navigation, language switcher, footer),
and adds a Manual link to the navigation of the site's other pages.
"""
import argparse
import html
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'shared/deskport-core'
# Site directory ('' is English at the root) -> manual language code.
SITE_LANGUAGES = {'': 'en', 'zh-CN': 'zh-Hans', 'zh-TW': 'zh-Hant', 'ja': 'ja', 'ko': 'ko',
                  'de': 'de', 'es': 'es', 'fr': 'fr', 'pt-BR': 'pt', 'ru': 'ru'}
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
    nav = re.search(r'<nav aria-label="Main">.*?</nav>', page, re.S)
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
    nav = re.search(r'<nav aria-label="Main">.*?</nav>', shell, re.S)
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
                text = page.read_text(encoding='utf-8')
                page.write_text(add_nav_link(text, label), encoding='utf-8')
        shell = (folder / 'support.html').read_text(encoding='utf-8')
        target = folder / 'manual.html'
        target.write_text(render(shell, manual, language, label), encoding='utf-8')
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
