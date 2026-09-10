#!/usr/bin/env python3
"""Check desktop catalog coverage and placeholder safety without personal state."""
from pathlib import Path
import re
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
languages = ("zh_CN", "zh_TW", "ja", "ko", "de", "fr", "es")
pages = ("SettingsHome", "DeviceCard", "HostView", "BindView", "SetupView", "BindingApproval")
for language in languages:
    tree = ET.parse(root / "app/languages" / f"qml_{language}.ts")
    contexts = {
        context.findtext("name"): {
            message.findtext("source"): message.find("translation")
            for message in context.findall("message")
        }
        for context in tree.getroot().findall("context")
    }
    for page in pages:
        source = (root / "app/gui" / f"{page}.qml").read_text()
        for text in re.findall(r'qsTr\("((?:[^"\\]|\\.)*)"\)', source):
            translation = contexts.get(page, {}).get(text)
            assert translation is not None, (language, page, text)
            assert translation.get("type") not in ("unfinished", "vanished", "obsolete"), (language, page, text)
            assert translation.text, (language, page, text)
            assert sorted(re.findall(r"%\d+", text)) == sorted(re.findall(r"%\d+", translation.text)), (language, page, text)
print("PASS: seven translated desktop catalogs cover all six pages with intact placeholders; English is the source language")
