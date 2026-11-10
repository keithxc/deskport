#!/usr/bin/env python3
"""Client-driven key repeat: flag unit test, host overlay on both pinned
sources, and the client's forwarding rules. No input is injected."""
from pathlib import Path
import os, subprocess, tarfile, tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-key-repeat-') as tmp:
    binary = Path(tmp) / 'key-repeat'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(root),
                    str(root / 'tests/key-repeat.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

    for archive in ('sunshine.tar.gz', 'sunshine-nix.tar.gz'):
        target = Path(tmp) / archive
        (target / 'src').mkdir(parents=True)
        with tarfile.open(root / 'host/vendor' / archive) as stream:
            members = [m for m in stream.getmembers() if m.name.endswith('src/input.cpp') and m.isfile()]
            assert len(members) == 1, archive
            original = stream.extractfile(members[0]).read().decode()
        (target / 'src/input.cpp').write_text(original)
        for _ in range(2):  # Applying twice changes nothing further.
            subprocess.run(['python3', str(root / 'host/overlays/patch-host-key-repeat.py'), str(target)], check=True)
        patched = (target / 'src/input.cpp').read_text()
        assert patched.count('#include "deskport/common/keyrepeat.h"') == 1

        begin = patched.index('  void passthrough(std::shared_ptr<input_t> &input, PNV_KEYBOARD_PACKET packet) {')
        body = patched[begin:patched.index('\n  }\n', begin)]
        # The bit is read and stripped before the held-key lookup and injection.
        strip = body.index('packet->flags = deskport::key_repeat::hostFlags(packet->flags);')
        assert body.index('deskport::key_repeat::clientDriven(packet->flags)') < strip
        assert strip < body.index('key_press[make_kpid(keyCode, packet->flags)]')
        # The host timer starts only for clients that do not drive repeat.
        assert body.count('key_repeat_delay.count() > 0 && !deskport_client_repeat') == 1
        assert 'if (config::input.key_repeat_delay.count() > 0) {' not in body
        # A repeated press re-emits the key only on a client's request.
        held = body[body.index('} else if (!release) {'):]
        held = held[:held.index('pressed = !release;')]
        assert held.count('send_key_and_modifiers(keyCode, false, packet->flags, synthetic_modifiers);') == 1
        assert held.index('if (deskport_client_repeat)') < held.index('send_key_and_modifiers(') < held.index('return;')

series = (root / 'host/series').read_text()
assert 'all            copy    host/common/keyrepeat.h      src/deskport/common/' in series
assert series.index('host/common/keyrepeat.h') < series.index('patch-host-key-repeat.py')

keyboard = (root / 'app/streaming/input/keyboard.cpp').read_text()
handler = keyboard[keyboard.index('void SdlInputHandler::handleKeyEvent('):keyboard.index('void SdlInputHandler::handleTextEvent(')]
# Repeats skip shortcuts and local text input, and reach the host only for a
# key it already holds.
assert 'if (!repeat && (event->state == SDL_PRESSED) &&' in handler
assert 'if (!repeat && m_LocalTextInput.consumeKey(*event))' in handler
assert '!m_KeysDown.contains(keyCode) || !deskport::key_repeat::repeatable(keyCode)' in handler
# Press, repeat and release share one set of flags on every send.
sources = keyboard + (root / 'app/streaming/input/input.cpp').read_text()
assert 'LiSendKeyboardEvent(' not in sources
assert sources.count('LiSendKeyboardEvent2(') == sources.count('char(deskport::key_repeat::kClientDriven)') == 3
print('PASS: client-driven key repeat flag, host overlay and client forwarding')
