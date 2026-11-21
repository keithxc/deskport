#pragma once
#include <functional>
class QObject;
class QMenu;
class QAction;

// DeskPort lives in the menu bar: the tray icon is the only entry point, so the
// application starts as an accessory (LSUIElement in Info.plist) and keeps no
// Dock tile. SDL turns the process back into a regular Dock application when it
// initializes video for a streaming session, which is what a session wants, so
// the accessory policy is restored once that session is over.
void deskPortSetDockIconVisible(bool visible);

// An accessory application is not activated by ordering a window front alone.
void deskPortActivateApplication();

// Qt Cocoa emits tray activation on mouseDown, inside NSStatusBarButton's
// tracking loop. Run only after that loop has returned (not in tracking mode).
// Destroying context cancels the callback.
void deskPortAfterStatusItemTracking(QObject* context, std::function<void()> callback);

// A menu attached to a status item opens on either mouse button and swallows the
// button action, which leaves no left click for the window toggle. The right
// button pops this native menu up at the pointer instead. Preserve submenus,
// enabled state and checks; return nullptr when dismissed.
QAction* deskPortShowStatusMenu(QMenu* source);
