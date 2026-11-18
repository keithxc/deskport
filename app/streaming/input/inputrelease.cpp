#include "inputrelease.h"
#include "gamepadstate.h"
#include <Limelight.h>
#include <initializer_list>

int releaseGamepadInput(GamepadState* states, int mask, bool remoteInput)
{
    int stopped = 0;
    for (int i=0; i<MAX_GAMEPADS; ++i) {
        auto& state = states[i];
        if (state.mouseEmulationTimer) {
            SDL_RemoveTimer(state.mouseEmulationTimer);
            state.mouseEmulationTimer = 0;
            ++stopped;
            if (remoteInput) for (int button : {BUTTON_LEFT, BUTTON_RIGHT, BUTTON_MIDDLE, BUTTON_X1, BUTTON_X2})
                LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, button);
        }
        state.buttons = 0; state.lt = state.rt = 0;
        state.lsX = state.lsY = state.rsX = state.rsY = 0;
        state.lastStartDownTime = 0; state.emulatedClickpadButtonDown = false;
        if (remoteInput && state.controller) LiSendMultiControllerEvent(state.index, mask, 0, 0, 0, 0, 0, 0, 0);
    }
    return stopped;
}
