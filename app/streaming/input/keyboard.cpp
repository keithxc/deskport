#ifndef DP_DISABLE_LOCAL_INPUT_ALIASES
#define DP_DISABLE_LOCAL_INPUT_ALIASES
#endif
#include "../../../shared/deskport-core/portable/include/deskport/input.h"

#include "streaming/session.h"
#include "streaming/canvasbridge.h"

#include <Limelight.h>
#include "../../../host/common/keyrepeat.h"
#include <SDL.h>

#define VK_0 0x30
#define VK_A 0x41

// These are real Windows VK_* codes
#ifndef VK_F1
#define VK_F1 0x70
#define VK_F13 0x7C
#define VK_NUMPAD0 0x60
#endif

void SdlInputHandler::performSpecialKeyCombo(KeyCombo combo)
{
    m_LocalTextInput.cancel();
    switch (combo) {
    case KeyComboQuit:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected quit key combo");

        // The native escape chord first gives local control back without
        // tearing down a fullscreen session. A second press disconnects.
        if (Session::get()->leaveFullscreen()) break;
        // Push a quit event to the main loop
        SDL_Event event;
        event.type = SDL_USEREVENT;
        event.user.code = DeskPortEndSession;
        event.user.timestamp = SDL_GetTicks();
        SDL_PushEvent(&event);
        break;

    case KeyComboUngrabInput:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected mouse capture toggle combo");

        // Stop handling future input
        setCaptureActive(!isCaptureActive());
        m_CaptureReleasedByUser = !isCaptureActive();

        // Force raise all keys to ensure they aren't stuck,
        // since we won't get their key up events.
        raiseAllKeys();
        break;

    case KeyComboToggleFullScreen:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected full-screen toggle combo");
        Session::s_ActiveSession->toggleFullscreen();

        // Force raise all keys just be safe across this full-screen/windowed
        // transition just in case key events get lost.
        raiseAllKeys();
        break;

    case KeyComboToggleStatsOverlay:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected stats toggle combo");

        // Toggle the stats overlay
        Session::get()->getOverlayManager().setOverlayState(Overlay::OverlayDebug,
                                                            !Session::get()->getOverlayManager().isOverlayEnabled(Overlay::OverlayDebug));
        break;

    case KeyComboToggleMouseMode:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected mouse mode toggle combo");

        // Uncapture input
        setCaptureActive(false);

        // Toggle mouse mode
        m_AbsoluteMouseMode = !m_AbsoluteMouseMode;

        // Recapture input
        setCaptureActive(true);
        break;

    case KeyComboToggleCursorHide:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected show mouse combo");

        if (!SDL_GetRelativeMouseMode()) {
            m_MouseCursorCapturedVisibilityState = !m_MouseCursorCapturedVisibilityState;
            SDL_ShowCursor(m_MouseCursorCapturedVisibilityState);
        }
        else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Cursor can only be shown in remote desktop mouse mode");
        }
        break;

    case KeyComboToggleMinimize:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected minimize combo");
        if (CanvasBridge::owns(m_Window)) emit Session::get()->canvasMinimizeRequested();
        else SDL_MinimizeWindow(m_Window);
        break;

    case KeyComboPasteText:
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected type clipboard text combo");

        // Force raise all keys to ensure that none of them interfere
        // with the text we're going to type.
        raiseAllKeys();

        char* text;
        if (SDL_HasClipboardText() && (text = SDL_GetClipboardText()) != nullptr) {
            // Sending both CR and LF will lead to two newlines in the destination for
            // each newline in the source, so we fix up any CRLFs into just a single LF.
            for (char* c = text; *c != 0; c++) {
                if (*c == '\r' && *(c + 1) == '\n') {
                    // We're using strlen() rather than strlen() - 1 since we need to add 1
                    // to copy the null terminator which is not included in strlen()'s count.
                    memmove(c, c + 1, strlen(c));
                }
            }

            // Send this text to the PC
            LiSendUtf8TextEvent(text, (unsigned int)strlen(text));

            // SDL_GetClipboardText() allocates, so we must free
            SDL_free((void*)text);
        }
        else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "No text in clipboard to paste!");
        }
        break;
    }

    case KeyComboTogglePointerRegionLock:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected pointer region lock toggle combo");
        m_PointerRegionLockActive = !m_PointerRegionLockActive;

        // Remember that the user changed this manually, so we don't mess with it anymore
        // during windowed <-> full-screen transitions.
        m_PointerRegionLockToggledByUser = true;

        // Apply the new region lock
        updatePointerRegionLock();
        break;

    case KeyComboNextSession:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected next desktop combo");
        Session::get()->requestNextViewer();
        break;

    default:
        Q_UNREACHABLE();
    }
}

void SdlInputHandler::handleKeyEvent(SDL_KeyboardEvent* event)
{
    const auto shortcut = m_ShortcutChord.handle(*event);
    if (shortcut != ShortcutState::KeyResult::Pass) {
        if (shortcut == ShortcutState::KeyResult::Pressed) releaseRemoteInput();
        else if (shortcut == ShortcutState::KeyResult::Released) Session::get()->requestShortcutToggle();
        return;
    }
    short keyCode;
    char modifiers;

    // The host does not time repeats for this client; the local keyboard's own
    // repeat is forwarded below for a key the host already holds.
    const bool repeat = event->repeat;
    SDL_assert(!repeat || event->state == SDL_PRESSED);

    // Check for our special key combos
    if (!repeat && (event->state == SDL_PRESSED) &&
            (event->keysym.mod & KMOD_CTRL) &&
            (event->keysym.mod & KMOD_ALT) &&
            (event->keysym.mod & KMOD_SHIFT)) {
        // First we test the SDLK combos for matches,
        // that way we ensure that latin keyboard users
        // can match to the key they see on their keyboards.
        // If nothing matches that, we'll then go on to
        // checking scancodes so non-latin keyboard users
        // can have working hotkeys (though possibly in
        // odd positions). We must do all SDLK tests before
        // any scancode tests to avoid issues in cases
        // where the SDLK for one shortcut collides with
        // the scancode of another.

        for (int i = 0; i < KeyComboMax; i++) {
            if (m_SpecialKeyCombos[i].enabled && event->keysym.sym == m_SpecialKeyCombos[i].keyCode) {
                performSpecialKeyCombo(m_SpecialKeyCombos[i].keyCombo);
                return;
            }
        }

        for (int i = 0; i < KeyComboMax; i++) {
            if (m_SpecialKeyCombos[i].enabled && event->keysym.scancode == m_SpecialKeyCombos[i].scanCode) {
                performSpecialKeyCombo(m_SpecialKeyCombos[i].keyCombo);
                return;
            }
        }
    }

    if (!keyboardRoutingActive()) return;
    if (!repeat && m_LocalTextInput.consumeKey(*event)) {
        // Local modifiers must not contaminate UTF-8 commits. Release any
        // synthesized command modifiers when the physical modifier is released.
        if (event->type == SDL_KEYUP && event->keysym.scancode >= SDL_SCANCODE_LCTRL &&
            event->keysym.scancode <= SDL_SCANCODE_RGUI) raiseAllKeys();
        return;
    }
    if (!repeat && m_LocalTextInput.active() && event->type == SDL_KEYDOWN) {
        // Hosts cannot all synthesize Meta from a packet flag. Explicit command
        // modifiers also preserve held chords until release/focus loss.
        const struct { int mask; short key; } commandMods[] = {
            {KMOD_CTRL, DP_VK_LCONTROL}, {KMOD_SHIFT, DP_VK_LSHIFT},
            {KMOD_ALT, DP_VK_LMENU}, {KMOD_GUI, DP_VK_LWIN}
        };
        for (const auto& modifier : commandMods) {
            if ((event->keysym.mod & modifier.mask) && !m_KeysDown.contains(modifier.key)) {
                LiSendKeyboardEvent2(0x8000 | modifier.key, KEY_ACTION_DOWN, 0, char(deskport::key_repeat::kClientDriven));
                m_KeysDown.insert(modifier.key);
            }
        }
    }

    // Set modifier flags
    modifiers = 0;
    if (event->keysym.mod & KMOD_CTRL) {
        modifiers |= MODIFIER_CTRL;
    }
    if (event->keysym.mod & KMOD_ALT) {
        modifiers |= MODIFIER_ALT;
    }
    if (event->keysym.mod & KMOD_SHIFT) {
        modifiers |= MODIFIER_SHIFT;
    }
    if (event->keysym.mod & KMOD_GUI) {
        if (isSystemKeyCaptureActive()) {
            modifiers |= MODIFIER_META;
        }
    }

    // Reuse matching portable WinVK constants only. Keep the full SDL mapping;
    // the portable validity whitelist is narrower than desktop keyboard support.
    // Set keycode. We explicitly use scancode here because GFE will try to correct
    // for AZERTY layouts on the host but it depends on receiving VK_ values matching
    // a QWERTY layout to work.
    if (event->keysym.scancode >= SDL_SCANCODE_1 && event->keysym.scancode <= SDL_SCANCODE_9) {
        // SDL defines SDL_SCANCODE_0 > SDL_SCANCODE_9, so we need to handle that manually
        keyCode = (event->keysym.scancode - SDL_SCANCODE_1) + VK_0 + 1;
    }
    else if (event->keysym.scancode >= SDL_SCANCODE_A && event->keysym.scancode <= SDL_SCANCODE_Z) {
        keyCode = (event->keysym.scancode - SDL_SCANCODE_A) + VK_A;
    }
    else if (event->keysym.scancode >= SDL_SCANCODE_F1 && event->keysym.scancode <= SDL_SCANCODE_F12) {
        keyCode = (event->keysym.scancode - SDL_SCANCODE_F1) + VK_F1;
    }
    else if (event->keysym.scancode >= SDL_SCANCODE_F13 && event->keysym.scancode <= SDL_SCANCODE_F24) {
        keyCode = (event->keysym.scancode - SDL_SCANCODE_F13) + VK_F13;
    }
    else if (event->keysym.scancode >= SDL_SCANCODE_KP_1 && event->keysym.scancode <= SDL_SCANCODE_KP_9) {
        // SDL defines SDL_SCANCODE_KP_0 > SDL_SCANCODE_KP_9, so we need to handle that manually
        keyCode = (event->keysym.scancode - SDL_SCANCODE_KP_1) + VK_NUMPAD0 + 1;
    }
    else {
        switch (event->keysym.scancode) {
            case SDL_SCANCODE_BACKSPACE:
                keyCode = DP_VK_BACK;
                break;
            case SDL_SCANCODE_TAB:
                keyCode = DP_VK_TAB;
                break;
            case SDL_SCANCODE_CLEAR:
                keyCode = 0x0C;
                break;
            case SDL_SCANCODE_KP_ENTER: // FIXME: Is this correct?
            case SDL_SCANCODE_RETURN:
                keyCode = DP_VK_RETURN;
                break;
            case SDL_SCANCODE_PAUSE:
                keyCode = DP_VK_PAUSE;
                break;
            case SDL_SCANCODE_CAPSLOCK:
                keyCode = DP_VK_CAPITAL;
                break;
            case SDL_SCANCODE_ESCAPE:
                keyCode = DP_VK_ESCAPE;
                break;
            case SDL_SCANCODE_SPACE:
                keyCode = DP_VK_SPACE;
                break;
            case SDL_SCANCODE_PAGEUP:
                keyCode = DP_VK_PRIOR;
                break;
            case SDL_SCANCODE_PAGEDOWN:
                keyCode = DP_VK_NEXT;
                break;
            case SDL_SCANCODE_END:
                keyCode = DP_VK_END;
                break;
            case SDL_SCANCODE_HOME:
                keyCode = DP_VK_HOME;
                break;
            case SDL_SCANCODE_LEFT:
                keyCode = DP_VK_LEFT;
                break;
            case SDL_SCANCODE_UP:
                keyCode = DP_VK_UP;
                break;
            case SDL_SCANCODE_RIGHT:
                keyCode = DP_VK_RIGHT;
                break;
            case SDL_SCANCODE_DOWN:
                keyCode = DP_VK_DOWN;
                break;
            case SDL_SCANCODE_SELECT:
                keyCode = 0x29;
                break;
            case SDL_SCANCODE_EXECUTE:
                keyCode = 0x2B;
                break;
            case SDL_SCANCODE_PRINTSCREEN:
                keyCode = DP_VK_SNAPSHOT;
                break;
            case SDL_SCANCODE_INSERT:
                keyCode = DP_VK_INSERT;
                break;
            case SDL_SCANCODE_DELETE:
                keyCode = DP_VK_DELETE;
                break;
            case SDL_SCANCODE_HELP:
                keyCode = 0x2F;
                break;
            case SDL_SCANCODE_KP_0:
                // See comment above about why we only handle SDL_SCANCODE_KP_0 here
                keyCode = VK_NUMPAD0;
                break;
            case SDL_SCANCODE_0:
                // See comment above about why we only handle SDL_SCANCODE_0 here
                keyCode = VK_0;
                break;
            case SDL_SCANCODE_KP_MULTIPLY:
                keyCode = 0x6A;
                break;
            case SDL_SCANCODE_KP_PLUS:
                keyCode = 0x6B;
                break;
            case SDL_SCANCODE_KP_COMMA:
                keyCode = 0x6C;
                break;
            case SDL_SCANCODE_KP_MINUS:
                keyCode = 0x6D;
                break;
            case SDL_SCANCODE_KP_PERIOD:
                keyCode = 0x6E;
                break;
            case SDL_SCANCODE_KP_DIVIDE:
                keyCode = 0x6F;
                break;
            case SDL_SCANCODE_NUMLOCKCLEAR:
                keyCode = DP_VK_NUMLOCK;
                break;
            case SDL_SCANCODE_SCROLLLOCK:
                keyCode = DP_VK_SCROLL;
                break;
            case SDL_SCANCODE_LSHIFT:
                keyCode = DP_VK_LSHIFT;
                break;
            case SDL_SCANCODE_RSHIFT:
                keyCode = DP_VK_RSHIFT;
                break;
            case SDL_SCANCODE_LCTRL:
                keyCode = DP_VK_LCONTROL;
                break;
            case SDL_SCANCODE_RCTRL:
                keyCode = DP_VK_RCONTROL;
                break;
            case SDL_SCANCODE_LALT:
                keyCode = DP_VK_LMENU;
                break;
            case SDL_SCANCODE_RALT:
                keyCode = DP_VK_RMENU;
                break;
            case SDL_SCANCODE_LGUI:
                if (!isSystemKeyCaptureActive()) {
                    return;
                }
                keyCode = DP_VK_LWIN;
                break;
            case SDL_SCANCODE_RGUI:
                if (!isSystemKeyCaptureActive()) {
                    return;
                }
                keyCode = DP_VK_RWIN;
                break;
            case SDL_SCANCODE_APPLICATION:
                keyCode = DP_VK_APPS;
                break;
            case SDL_SCANCODE_AC_BACK:
                keyCode = 0xA6;
                break;
            case SDL_SCANCODE_AC_FORWARD:
                keyCode = 0xA7;
                break;
            case SDL_SCANCODE_AC_REFRESH:
                keyCode = 0xA8;
                break;
            case SDL_SCANCODE_AC_STOP:
                keyCode = 0xA9;
                break;
            case SDL_SCANCODE_AC_SEARCH:
                keyCode = 0xAA;
                break;
            case SDL_SCANCODE_AC_BOOKMARKS:
                keyCode = 0xAB;
                break;
            case SDL_SCANCODE_AC_HOME:
                keyCode = 0xAC;
                break;
            case SDL_SCANCODE_SEMICOLON:
                keyCode = DP_VK_OEM_1;
                break;
            case SDL_SCANCODE_EQUALS:
                keyCode = DP_VK_OEM_PLUS;
                break;
            case SDL_SCANCODE_COMMA:
                keyCode = DP_VK_OEM_COMMA;
                break;
            case SDL_SCANCODE_MINUS:
                keyCode = DP_VK_OEM_MINUS;
                break;
            case SDL_SCANCODE_PERIOD:
                keyCode = DP_VK_OEM_PERIOD;
                break;
            case SDL_SCANCODE_SLASH:
                keyCode = DP_VK_OEM_2;
                break;
            case SDL_SCANCODE_GRAVE:
                keyCode = DP_VK_OEM_3;
                break;
            case SDL_SCANCODE_LEFTBRACKET:
                keyCode = DP_VK_OEM_4;
                break;
            case SDL_SCANCODE_BACKSLASH:
                keyCode = DP_VK_OEM_5;
                break;
            case SDL_SCANCODE_RIGHTBRACKET:
                keyCode = DP_VK_OEM_6;
                break;
            case SDL_SCANCODE_APOSTROPHE:
                keyCode = DP_VK_OEM_7;
                break;
            case SDL_SCANCODE_NONUSBACKSLASH:
                keyCode = DP_VK_OEM_102;
                break;
            default:
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Unhandled button event: %d",
                             event->keysym.scancode);
                return;
        }
    }

    // Track the key state so we always know which keys are down
    if (repeat) {
        // Keys consumed locally or by a shortcut were never pressed on the host.
        if (!m_KeysDown.contains(keyCode) || !deskport::key_repeat::repeatable(keyCode)) return;
    }
    else if (event->state == SDL_PRESSED) {
        m_KeysDown.insert(keyCode);
    }
    else if (!m_KeysDown.remove(keyCode)) {
        return; // Already released on exit/focus loss, or pressed locally.
    }

    // Every key event carries the same flags; older hosts match a release to
    // its press by code and flags.
    LiSendKeyboardEvent2(0x8000 | keyCode,
                         event->state == SDL_PRESSED ?
                             KEY_ACTION_DOWN : KEY_ACTION_UP,
                         modifiers, char(deskport::key_repeat::kClientDriven));
}

void SdlInputHandler::handleTextEvent(const SDL_Event& event)
{
    if (!keyboardRoutingActive()) return;
    const auto window = SDL_GetWindowID(m_Window);
    if (event.type == SDL_TEXTINPUT)
        m_LocalTextInput.text(event.text.windowID, event.text.timestamp, event.text.text, window);
    else if (event.type == SDL_TEXTEDITING)
        m_LocalTextInput.editing(event.edit.windowID, event.edit.timestamp, event.edit.text,
                                event.edit.start, event.edit.length, window);
#if SDL_VERSION_ATLEAST(2, 0, 22)
    else if (event.type == SDL_TEXTEDITING_EXT)
        m_LocalTextInput.editing(event.editExt.windowID, event.editExt.timestamp, event.editExt.text,
                                event.editExt.start, event.editExt.length, window);
#endif
}
