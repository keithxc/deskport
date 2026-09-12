#pragma once

// DeskPort lives in the menu bar: the tray icon is the only entry point, so the
// application starts as an accessory (LSUIElement in Info.plist) and keeps no
// Dock tile. SDL turns the process back into a regular Dock application when it
// initializes video for a streaming session, which is what a session wants, so
// the accessory policy is restored once that session is over.
void deskPortSetDockIconVisible(bool visible);

// An accessory application is not activated by ordering a window front alone.
void deskPortActivateApplication();
