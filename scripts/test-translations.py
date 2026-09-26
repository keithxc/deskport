#!/usr/bin/env python3
"""Check desktop catalog coverage and placeholder safety without personal state."""
from pathlib import Path
import re
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
languages = ("zh_CN", "zh_TW", "ja", "ko", "de", "fr", "es")
pages = ("main", "DeviceSettings", "DeviceAdvanced", "SettingsHome", "DeviceCard", "HostView", "BindView", "PeerEditor", "SetupView", "BindingApproval", "DesktopSegue", "StreamSegue")
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
print("PASS: seven translated desktop catalogs cover all primary pages with intact placeholders; English is the source language")
