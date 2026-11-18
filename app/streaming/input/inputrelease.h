#pragma once
struct GamepadState;
// Neutralize global gamepad input and stop mouse-emulation timers. Returns the
// number of stopped emulators so the session can clear its overlay state.
int releaseGamepadInput(GamepadState* states, int mask, bool remoteInput = true);
