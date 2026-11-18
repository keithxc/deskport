#!/usr/bin/env python3
"""Check desktop catalog coverage and placeholder safety without personal state."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

from pathlib import Path
import re
import json
import subprocess
import sys
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, str(root/'scripts/sync-translations.py'), '--check'], check=True)
languages = [language['qt'] for language in json.loads((root/'shared/deskport-core/i18n/languages.json').read_text()) if language['qt'] != 'en']
pages = ("main", "PcView", "DeviceCard", "DevicePanel", "DeviceSettings", "DeviceAdvanced", "SettingsHome", "ShortcutDialog", "HelpView", "NavigableDialog", "HostView", "BindView", "PeerEditor", "SetupView", "BindingApproval", "DesktopSegue", "StreamSegue")
for language in languages:
    tree = ET.parse(root / "app/languages" / f"qml_{language}.ts")
    contexts = {
        context.findtext("name"): {
            message.findtext("source"): message.find("translation")
            for message in context.findall("message")
        }
        for context in tree.getroot().findall("context")
    }
    messages_to_check = []
    for page in pages:
        source = (root / "app/gui" / f"{page}.qml").read_text()
        messages = [(page, text) for text in re.findall(r'qsTr\("((?:[^"\\]|\\.)*)"\)', source)]
        messages += re.findall(r'qsTranslate\("([^"]+)",\s*"((?:[^"\\]|\\.)*)"', source)
        messages_to_check += messages
    for source_file in ("streaming/input/input.cpp", "backend/clipboardchannel.cpp"):
        source = (root / "app" / source_file).read_text()
        messages_to_check += re.findall(r'QCoreApplication::translate\("([^"]+)",\s*"((?:[^"\\]|\\.)*)"', source)
    messages_to_check += [("Session", text) for text in (
        "Connecting to desktop…",
        "This connection would create a loop. Disconnect one of the existing links first.",
        "The connection path could not be verified. Update DeskPort on every desktop in the chain and try again.",
    )]
    for context, text in messages_to_check:
        translation = contexts.get(context, {}).get(text)
        assert translation is not None, (language, context, text)
        assert translation.get("type") not in ("unfinished", "vanished", "obsolete"), (language, context, text)
        assert translation.text, (language, context, text)
        assert sorted(re.findall(r"%\d+", text)) == sorted(re.findall(r"%\d+", translation.text)), (language, context, text)
print(f"PASS: {len(languages)} translated desktop catalogs cover all primary pages with intact placeholders; English is the source language")
