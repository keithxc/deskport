#define SDL_MAIN_HANDLED
#include <QtTest>
#include <QApplication>
#include <QWidget>
#include <future>
#include <thread>
#include "input/inputtimergate.h"
#ifdef Q_OS_WIN
#include <SDL_syswm.h>
#include <windows.h>
#include <dwmapi.h>
#endif
#include "vieweractivity.h"
#include "windowvisibility.h"

#ifdef Q_OS_MACOS
void* createVisibilityCover(SDL_Window*);
void showVisibilityCover(void*,bool);
void destroyVisibilityCover(void*);
bool nativeVisibilityOccluded(SDL_Window*);
#endif

// Production visibility reads authenticated canvas flags instead of the hidden
// SDL decoder context. Doubles below keep this test independent of shell IPC.
namespace CanvasBridge {
static bool testCanvas = false;
static Uint32 testFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS;
bool owns(SDL_Window*) { return testCanvas; }
Uint32 flags(SDL_Window* window) { return testCanvas ? testFlags : SDL_GetWindowFlags(window); }
}

class ViewerActivityTest : public QObject {
    Q_OBJECT
private slots:
    void hideWaitsForInputTimerSendsAndBlocksLaterCallbacks() {
        InputTimerGate gate;
        void* oldContext=gate.context(7);
        std::promise<void> entered, release, hidden;
        auto finish=release.get_future().share();
        auto stopped=hidden.get_future();
        int sent=0;
        std::thread timer([&] { InputTimerGate::dispatch(oldContext, [&](void*,unsigned) { entered.set_value(); finish.wait(); ++sent; }); });
        entered.get_future().wait();
        std::thread hide([&] { gate.suspend(); hidden.set_value(); });
        // Hide cannot acknowledge until the already-running send finishes.
        const bool waited=stopped.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout;
        release.set_value(); timer.join(); hide.join();
        QVERIFY(waited); QCOMPARE(sent,1);
        for (int i=0;i<1000;++i) QVERIFY(!gate.run([&] { ++sent; }));
        QCOMPARE(sent,1);
        gate.resume();
        QVERIFY(!InputTimerGate::dispatch(oldContext, [&](void*,unsigned) { ++sent; }));
        QVERIFY(InputTimerGate::dispatch(gate.context(7), [&](void*,unsigned slot) { QCOMPARE(slot,7u); ++sent; }));
        QCOMPARE(sent,2);
        void* destroyedContext;
        { InputTimerGate temporary; destroyedContext=temporary.context(); }
        QVERIFY(!InputTimerGate::dispatch(destroyedContext, [&](void*,unsigned) { ++sent; }));
        QCOMPARE(sent,2);
    }
    void queuedHideAfterRestoreReestablishesInputOnce() {
        ViewerActivity state;
        state.update(true,false,true,0);
        QVERIFY(!state.takeInputChange());
        // SDL flags already reflect Show, but the earlier Hidden event still
        // releases input while it is drained from the native event queue.
        state.inputSuspended();
        state.update(true,false,true,1);
        QVERIFY(state.available()); QVERIFY(state.takeInputChange());
        QVERIFY(!state.takeInputChange()); QVERIFY(!state.videoPaused(10000));
        state.update(true,true,false,2);
        QVERIFY(state.takeInputChange()); QVERIFY(!state.available());
        state.inputSuspended();
        state.update(true,false,true,3);
        QVERIFY(state.takeInputChange()); QVERIFY(state.available());
        QVERIFY(!state.takeInputChange());
    }
    void hiddenMinimizedOccludedAndLockedShareOneDeadline() {
        ViewerActivity state;
        state.update(true, false, true, 0);
        QVERIFY(state.available()); QVERIFY(!state.videoPaused(10000));
        // The cause can change from hidden to minimized/locked; repeated
        // notifications must not postpone pausing indefinitely.
        state.update(false, false, false, 10000);
        state.update(true, true, false, 12000);
        state.update(true, true, false, 14999);
        QVERIFY(!state.available()); QVERIFY(!state.videoPaused(14999));
        QVERIFY(state.videoPaused(15000));
        state.update(true, false, true, 15001);
        QVERIFY(state.available()); QVERIFY(!state.videoPaused(15001));
    }
    void repeatedRecallAndFocusLossDoNotPauseVisibleVideo() {
        ViewerActivity state;
        for (uint64_t time = 0; time < 100000; time += 1000) {
            state.update(false, false, false, time);
            QVERIFY(!state.available());
            state.update(true, false, true, time + 999);
            QVERIFY(state.available()); QVERIFY(!state.videoPaused(time + 999));
        }
        state.update(true, false, false, 200000);
        QVERIFY(state.available()); QVERIFY(!state.videoPaused(300000));
    }
    void audioPreferences_data() {
        QTest::addColumn<bool>("receive"); QTest::addColumn<bool>("muteOnLoss");
        for (bool receive : {false,true}) for (bool mute : {false,true})
            QTest::newRow(qPrintable(QString("receive-%1-mute-%2").arg(receive).arg(mute))) << receive << mute;
    }
    void audioPreferences() {
        QFETCH(bool,receive); QFETCH(bool,muteOnLoss); ViewerActivity state;
        for (bool requested : {false,true}) for (bool unavailable : {false,true}) for (bool focus : {false,true}) {
            state.update(requested, unavailable, focus, 0);
            QCOMPARE(state.audioMuted(receive,muteOnLoss), !receive || (muteOnLoss && !(requested && !unavailable && focus)));
        }
    }
    void canvasVisibilityUsesShellState() {
        CanvasBridge::testCanvas = true;
        auto window = reinterpret_cast<SDL_Window*>(1); // Never dereferenced in canvas mode.
        WindowVisibility visibility(window);
        QVERIFY(!visibility.unavailable());
        CanvasBridge::testFlags = SDL_WINDOW_HIDDEN;
        QVERIFY(visibility.unavailable());
        CanvasBridge::testFlags = SDL_WINDOW_SHOWN;
        QVERIFY(!visibility.unavailable());
        CanvasBridge::testCanvas = false;
    }
    void nativeWindowLifetimeAndHideRecall() {
        QVERIFY2(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0, SDL_GetError());
        auto window = SDL_CreateWindow("DeskPort isolated visibility fixture", 80,80,400,300, SDL_WINDOW_HIDDEN | SDL_WINDOW_ALWAYS_ON_TOP);
        QVERIFY2(window, SDL_GetError());
        {
            WindowVisibility visibility(window);
            qInfo("Detected capability: %s", visibility.capability());
            QVERIFY(visibility.unavailable());
            for (int i=0;i<20;++i) {
                SDL_ShowWindow(window); SDL_RaiseWindow(window);
                QTRY_VERIFY_WITH_TIMEOUT(([&] { SDL_PumpEvents(); return !visibility.unavailable(); })(),5000);
                SDL_HideWindow(window);
                QTRY_VERIFY_WITH_TIMEOUT(([&] { SDL_PumpEvents(); return visibility.unavailable(); })(),5000);
            }
            SDL_ShowWindow(window); SDL_RaiseWindow(window);
            QTRY_VERIFY_WITH_TIMEOUT(([&] { SDL_PumpEvents(); return !visibility.unavailable(); })(),5000);
            if (qEnvironmentVariableIsSet("DESKPORT_NATIVE_VISIBILITY_TEST")) {
                SDL_MinimizeWindow(window);
                const bool minimized = QTest::qWaitFor([&] { SDL_PumpEvents(); return (SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED) != 0; },5000);
                if (minimized) { QVERIFY(visibility.unavailable()); SDL_RestoreWindow(window); SDL_RaiseWindow(window);
                    QTRY_VERIFY_WITH_TIMEOUT(([&] { SDL_PumpEvents(); return !visibility.unavailable(); })(),5000); }
                else qInfo("Minimize event unavailable: explicit capability downgrade; no polling workaround");
#ifdef Q_OS_MACOS
                if (QByteArray(visibility.capability()).startsWith("cocoa-occlusion")) {
                    const auto deleter=[](void* cover) { destroyVisibilityCover(cover); };
                    std::unique_ptr<void,decltype(deleter)> cover(createVisibilityCover(window),deleter);
                    QVERIFY(cover);
                    for (int i=0;i<3;++i) {
                        showVisibilityCover(cover.get(),true);
                        const bool occluded=QTest::qWaitFor([&] { SDL_PumpEvents(); return visibility.unavailable(); },5000);
                        qInfo("Cocoa target raw occluded=%d, observer unavailable=%d",nativeVisibilityOccluded(window),visibility.unavailable());
                        QVERIFY(occluded);
                        showVisibilityCover(cover.get(),false); SDL_RaiseWindow(window);
                        QTRY_VERIFY_WITH_TIMEOUT(([&] { SDL_PumpEvents(); return !visibility.unavailable(); })(),5000);
                    }
                    qInfo("Native occlusion and reveal: 3 cycles passed");
                }
#endif
#ifdef Q_OS_WIN
                if (QByteArray(visibility.capability()).startsWith("dwm-cloak")) {
                    SDL_SysWMinfo info {}; SDL_VERSION(&info.version); QVERIFY(SDL_GetWindowWMInfo(window,&info));
                    for (BOOL cloak : {TRUE,FALSE}) {
                        QVERIFY(SUCCEEDED(DwmSetWindowAttribute(info.info.win.window,DWMWA_CLOAK,&cloak,sizeof(cloak))));
                        QTRY_COMPARE_WITH_TIMEOUT(([&] { SDL_PumpEvents(); return visibility.unavailable(); })(),bool(cloak),5000);
                    }
                    qInfo("Native DWM cloak and reveal passed");
                }
#endif
            }
        }
        // A new observer on the retained window must snapshot its current state.
        { WindowVisibility resumed(window);
          QTRY_VERIFY_WITH_TIMEOUT(([&] { SDL_PumpEvents(); return !resumed.unavailable(); })(),5000); }
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};
int main(int argc,char**argv) {
    SDL_SetMainReady();
    QApplication app(argc,argv); ViewerActivityTest tests;
    return QTest::qExec(&tests,argc,argv);
}
#include "viewer-activity.moc"
