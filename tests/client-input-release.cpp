#include <QtTest>
#include "streaming/input/gamepadstate.h"
#include "streaming/input/inputrelease.h"
#include <Limelight.h>

namespace {
QStringList events;
GamepadState* observed;
}
extern "C" SDL_bool SDL_RemoveTimer(SDL_TimerID timer) {
    // Polling is stopped before its state is cleared; no real timer/device.
    if (timer == 7 && observed[0].buttons == A_FLAG) events << "stop";
    return SDL_TRUE;
}
int LiSendMouseButtonEvent(char action, int button) {
    if (action == BUTTON_ACTION_RELEASE) events << QString("mouse-%1").arg(button);
    return 0;
}
int LiSendMultiControllerEvent(short controller, short mask, int buttons, unsigned char lt, unsigned char rt,
                              short lx, short ly, short rx, short ry) {
    if (mask == 3 && !buttons && !lt && !rt && !lx && !ly && !rx && !ry)
        events << QString("neutral-%1").arg(controller);
    return 0;
}
class ClientInputReleaseTest : public QObject {
    Q_OBJECT
private slots:
    void neutralizesEveryControllerAndStopsPolling() {
        GamepadState states[MAX_GAMEPADS]{}; observed = states;
        states[0].controller = reinterpret_cast<SDL_GameController*>(1);
        states[0].index = 0; states[0].buttons = A_FLAG;
        states[0].mouseEmulationTimer = 7; states[0].lsX = 12000;
        states[0].lastStartDownTime = 20; states[0].emulatedClickpadButtonDown = true;
        states[1].controller = reinterpret_cast<SDL_GameController*>(2);
        states[1].index = 1; states[1].rt = 255; states[1].rsY = -12000;
        QCOMPARE(releaseGamepadInput(states, 3),1);
        QCOMPARE(events.first(),QString("stop"));
        QCOMPARE(events.size(),8); // Timer, five emulated mouse buttons, two controllers.
        QVERIFY(events.contains("neutral-0")); QVERIFY(events.contains("neutral-1"));
        QCOMPARE(states[0].buttons,0); QCOMPARE(states[0].lsX,short(0));
        QCOMPARE(states[0].mouseEmulationTimer,SDL_TimerID(0));
        QVERIFY(!states[0].emulatedClickpadButtonDown);
        QCOMPARE(states[0].lastStartDownTime,uint32_t(0));
        QCOMPARE(states[1].rt,static_cast<unsigned char>(0)); QCOMPARE(states[1].rsY,short(0));
        events.clear(); QCOMPARE(releaseGamepadInput(states,3),0);
        QCOMPARE(events,QStringList({"neutral-0","neutral-1"}));
    }
};
QTEST_GUILESS_MAIN(ClientInputReleaseTest)
#include "client-input-release.moc"
