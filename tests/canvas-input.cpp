#include "streaming/canvasbridge.h"
#include "gui/canvaskey.h"
#include <QCoreApplication>
#include <cassert>
#include <cstdio>
#include <vector>

static std::vector<SDL_Event> events() {
    std::vector<SDL_Event> result; SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) > 0) {
        CanvasBridge::decodeTextEvent(event);
        result.push_back(event);
    }
    return result;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    SDL_Event unrelated{}; unrelated.type = SDL_USEREVENT; unrelated.user.code = 106;
    assert(!CanvasBridge::decodeTextEvent(unrelated) && unrelated.user.code == 106);
    assert(CanvasKey::windows(0x1e) == SDL_SCANCODE_A);
    assert(CanvasKey::windows(0x11d) == SDL_SCANCODE_RCTRL);
    assert(CanvasKey::windows(0x11c) == SDL_SCANCODE_KP_ENTER);
    assert(CanvasKey::windows(0x14b) == SDL_SCANCODE_LEFT);
    assert(CanvasKey::evdev(30) == SDL_SCANCODE_A);
    assert(CanvasKey::evdev(100) == SDL_SCANCODE_RALT);
    assert(CanvasKey::evdev(105) == SDL_SCANCODE_LEFT);
    assert(CanvasKey::evdev(194) == SDL_SCANCODE_F24);
    assert(CanvasKey::mac(0) == SDL_SCANCODE_A);
    assert(CanvasKey::mac(12) == SDL_SCANCODE_Q);
    assert(CanvasKey::mac(62) == SDL_SCANCODE_RCTRL);
    assert(CanvasKey::mac(76) == SDL_SCANCODE_KP_ENTER);
    SDL_SetMainReady(); SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
#ifdef Q_OS_MACOS
    QKeyEvent command(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
    assert(CanvasKey::physical(command, SDLK_LGUI) == SDL_SCANCODE_LGUI);
    QKeyEvent rightCommand(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier, 0, 54, 0);
    assert(CanvasKey::physical(rightCommand, SDLK_LGUI) == SDL_SCANCODE_RGUI);
    QKeyEvent physicalA(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, 0, 0, 0);
    assert(CanvasKey::physical(physicalA, SDLK_a) == SDL_SCANCODE_A);
#endif
    auto window = SDL_CreateWindow("isolated input fixture", 0, 0, 800, 600, SDL_WINDOW_HIDDEN);
    assert(window);
    QJsonObject state{{"width",800},{"height",600},{"scale",1},{"visible",true},{"focused",true},{"hovered",true}};
    CanvasBridge::configure(state); CanvasBridge::requested(true); CanvasBridge::registerWindow(window);
    CanvasBridge::inputEnabled(true); events();
    QJsonObject key{{"type","key"},{"down",true},{"scan",SDL_SCANCODE_A},{"symbol",SDLK_a},{"modifiers",0}};
    CanvasBridge::input(key);
    // Input must wake a blocked SDL_WaitEventTimeout loop immediately, and
    // synchronize() must drain the wakeup sentinel cleanly.
    auto woken = events();
    assert(woken.size() == 1 && CanvasBridge::isWakeEvent(woken[0]));
    CanvasBridge::synchronize(window);
    auto received = events(); assert(received.size() == 1 && received[0].type == SDL_KEYDOWN);
    // Consecutive motions coalesce while updating cursor coordinates; buttons stay ordered barriers.
    CanvasBridge::input({{"type","motion"},{"x",100},{"y",120},{"dx",5},{"dy",-3}});
    CanvasBridge::input({{"type","motion"},{"x",112},{"y",114},{"dx",12},{"dy",-6}});
    int mx = 0, my = 0;
    CanvasBridge::mouseState(&mx, &my);
    assert(mx == 112 && my == 114);
    CanvasBridge::input({{"type","button"},{"button",1},{"down",true},{"x",112},{"y",114}});
    CanvasBridge::input({{"type","motion"},{"x",120},{"y",118},{"dx",8},{"dy",4}});
    CanvasBridge::synchronize(window);
    received = events();
    assert(received.size() == 3);
    assert(received[0].type == SDL_MOUSEMOTION && received[0].motion.x == 112 && received[0].motion.y == 114 &&
           received[0].motion.xrel == 17 && received[0].motion.yrel == -9);
    assert(received[1].type == SDL_MOUSEBUTTONDOWN && received[1].button.button == 1);
    assert(received[2].type == SDL_MOUSEMOTION && received[2].motion.x == 120 && received[2].motion.y == 118 &&
           received[2].motion.xrel == 8 && received[2].motion.yrel == 4);
    // Losing and regaining focus before the next worker tick must still release.
    state["focused"] = false; CanvasBridge::state(state);
    state["focused"] = true; CanvasBridge::state(state); CanvasBridge::input(key);
    CanvasBridge::synchronize(window); received = events();
    assert(received.size() == 2);
    assert(received[0].window.event == SDL_WINDOWEVENT_FOCUS_LOST);
    assert(received[1].window.event == SDL_WINDOWEVENT_FOCUS_GAINED);
    // A flooded queue must preserve this boundary even after a fresh viewport.
    for (int i = 0; i < 257; ++i) CanvasBridge::input(key);
    CanvasBridge::input(key); // Input arriving before the release is discarded too.
    CanvasBridge::state(state); CanvasBridge::synchronize(window); received = events();
    assert(received.size() == 2 && received[0].window.event == SDL_WINDOWEVENT_FOCUS_LOST &&
           received[1].window.event == SDL_WINDOWEVENT_FOCUS_GAINED);
    CanvasBridge::input(key); CanvasBridge::synchronize(window); received = events();
    assert(received.size() == 1 && received[0].type == SDL_KEYDOWN);
    const auto unicode = QString::fromUtf8("中文🙂中文🙂中文🙂中文🙂中文🙂中文🙂");
    CanvasBridge::input({{"type","text"},{"text",unicode}}); CanvasBridge::synchronize(window);
    QByteArray text;
    for (const auto& event : events()) {
        assert(event.type == SDL_TEXTINPUT);
        QByteArray chunk(event.text.text);
        assert(QString::fromUtf8(chunk).toUtf8() == chunk); text += chunk;
    }
    assert(QString::fromUtf8(text) == unicode);
    CanvasBridge::pointerRegion(true, 10, 20, 300, 200);
    assert(CanvasBridge::description()["pointerRegion"].toObject()["width"].toInt() == 300);
    CanvasBridge::pointerRegion(false);
    assert(CanvasBridge::description()["pointerRegion"].toObject().isEmpty());
    CanvasBridge::input({{"type","touch"},{"phase","down"},{"id",12},{"x",0.25},{"y",0.5},
                         {"dx",0.0},{"dy",0.0},{"pressure",0.75}});
    CanvasBridge::input({{"type","touch"},{"phase","move"},{"id",12},{"x",2.0},{"y",0.5}});
    CanvasBridge::input({{"type","touch-cancel"}});
    CanvasBridge::synchronize(window); received = events();
    assert(received.size() == 2 && received[0].type == SDL_FINGERDOWN);
    assert(received[0].tfinger.touchId == CanvasBridge::SyntheticTouchId && received[0].tfinger.fingerId == 13);
    assert(received[0].tfinger.x == 0.25f && received[0].tfinger.pressure == 0.75f);
    assert(CanvasBridge::isTouchCancel(received[1]));
    CanvasBridge::question("Title", "Body", "Cancel", "Continue");
    auto id = quint64(CanvasBridge::description()["question"].toObject()["id"].toDouble());
    CanvasBridge::answer(id + 1, 1); assert(CanvasBridge::takeAnswer() == -1);
    CanvasBridge::answer(id, 2); assert(CanvasBridge::takeAnswer() == -1);
    CanvasBridge::answer(id, 1); assert(CanvasBridge::takeAnswer() == 1);
    CanvasBridge::dismissQuestion(); assert(!CanvasBridge::description().contains("question"));
    CanvasBridge::requested(false); CanvasBridge::input(key); CanvasBridge::synchronize(window);
    for (const auto& event : events()) assert(event.type != SDL_KEYDOWN);
    assert(SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN);
    SDL_DestroyWindow(window); SDL_Quit();
    puts("PASS: focus-loss boundaries, overflow release, hidden input rejection, Unicode chunks and stale question answers");
}
