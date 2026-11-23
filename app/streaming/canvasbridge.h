#pragma once
#include <SDL.h>

#include <QJsonObject>
#include <QSize>
#include <functional>
class QObject;

// Authenticated shell/worker IPC supplies presentation state. SDL's real
// window remains hidden and provides only the decoder's platform context.
namespace CanvasBridge {
// Retain authenticated transport credentials before worker startup clears them
// from the environment. Decoder recreation uses the same session credentials.
void setVideoTransport(const QString& endpoint, const QString& token);
QPair<QString, QString> videoTransport();
void configure(const QJsonObject& state);
bool enabled();
void state(const QJsonObject& state);
void requested(bool visible);
bool requested();
void registerWindow(SDL_Window* window);
bool owns(SDL_Window* window);
void synchronize(SDL_Window* window);
// Restore owned text and high-res wheel envelopes after SDL polling and before visibility checks.
bool decodeTextEvent(SDL_Event& event);
bool decodeWheelEvent(SDL_Event& event);
bool isTouchCancel(const SDL_Event& event);
bool isWakeEvent(const SDL_Event& event);
// Pump Qt IPC and SDL through one wait on the SDL/Qt owner thread.
bool waitForEvent(SDL_Event* event, int timeoutMs);
int idleWaitTimeout(bool gamepadAttached, bool viewerAvailable, bool canvasViewer);
constexpr SDL_TouchID SyntheticTouchId = -0x445054;
Uint32 flags(SDL_Window* window);
Uint32 mouseState(int* x, int* y);
QSize pixels();
qreal scale();
bool fullscreen();
void input(const QJsonObject& event);
// Admission and queued input share a lock. Disabling latches one release fence;
// re-enabling never erases it, and repeated disables do not wake the idle loop.
void inputEnabled(bool enabled);
void capture(bool grab, bool cursorHidden, bool relative = false);
void pointerRegion(bool enabled, int x = 0, int y = 0, int width = 0, int height = 0);
void overlay(int type, bool enabled, const QString& text);
void textInput(bool enabled);
void label(const QString& text);
void waiting(bool value);
QJsonObject description();
// Delivery is coalesced on context's thread. Clear before destroying context.
void setDescriptionCallback(QObject* context, std::function<void()> callback);
void question(const QString& title, const QString& text, const QString& cancel, const QString& accept);
void answer(quint64 id, int choice);
int takeAnswer();
void dismissQuestion();
}
