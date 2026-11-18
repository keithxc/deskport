#include "sleepmonitor.h"
#include <windows.h>
#include <wtsapi32.h>
#include <QDebug>
#include <QTimer>

namespace {
struct Watch {
    HWND window = nullptr;
    HPOWERNOTIFY power = nullptr;
    SleepMonitor* sleep = nullptr;
    ScreenLockMonitor* lock = nullptr;
};
void refreshLock(ScreenLockMonitor* monitor) {
    LPWSTR data = nullptr; DWORD size = 0;
    bool locked = true;
    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSSessionInfoEx, &data, &size)) {
        auto info = reinterpret_cast<WTSINFOEXW*>(data);
        if (size >= sizeof(WTSINFOEXW) && info->Level == 1)
            locked = info->Data.WTSInfoExLevel1.SessionFlags != WTS_SESSIONSTATE_UNLOCK;
        WTSFreeMemory(data);
    }
    monitor->setLocked(locked); // Unknown state cannot authorize input.
}
void watchSession(Watch* watch) {
    if (WTSRegisterSessionNotification(watch->window, NOTIFY_FOR_THIS_SESSION)) {
        refreshLock(watch->lock);
    } else {
        // Terminal Services may not be ready during login startup. Remain
        // blocked and retry without an interactive prompt or a service change.
        watch->lock->setLocked(true);
        QTimer::singleShot(1000, watch->lock, [watch] { watchSession(watch); });
    }
}
LRESULT CALLBACK events(HWND window, UINT message, WPARAM value, LPARAM detail) {
    auto watch = reinterpret_cast<Watch*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        watch = static_cast<Watch*>(reinterpret_cast<CREATESTRUCTW*>(detail)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(watch));
    }
    if (watch && message == WM_POWERBROADCAST && watch->sleep) {
        if (value == PBT_APMSUSPEND) watch->sleep->prepareForSleep(true);
        if (value == PBT_APMRESUMEAUTOMATIC || value == PBT_APMRESUMESUSPEND) watch->sleep->prepareForSleep(false);
        return TRUE;
    }
    if (watch && message == WM_WTSSESSION_CHANGE && watch->lock) {
        if (value == WTS_SESSION_LOCK || value == WTS_SESSION_LOGOFF || value == WTS_CONSOLE_DISCONNECT || value == WTS_REMOTE_DISCONNECT)
            watch->lock->setLocked(true);
        if (value == WTS_SESSION_UNLOCK) watch->lock->setLocked(false);
        if (value == WTS_CONSOLE_CONNECT || value == WTS_REMOTE_CONNECT || value == WTS_SESSION_LOGON)
            refreshLock(watch->lock);
    }
    return DefWindowProcW(window, message, value, detail);
}
Watch* create() {
    static const wchar_t name[] = L"DeskPortClientLifecycle";
    WNDCLASSW type{}; type.lpfnWndProc = events; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = name;
    RegisterClassW(&type);
    auto watch = new Watch;
    // A dedicated message window receives events even with every viewer hidden.
    watch->window = CreateWindowExW(0, name, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, type.hInstance, watch);
    if (!watch->window) { qWarning() << "Client lifecycle window unavailable"; delete watch; return nullptr; }
    return watch;
}
}
void* deskportWatchSleep(SleepMonitor* monitor) {
    auto watch = create(); if (!watch) return nullptr;
    watch->sleep = monitor;
    watch->power = RegisterSuspendResumeNotification(watch->window, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!watch->power) qWarning() << "Client power notifications unavailable";
    return watch;
}
void* deskportWatchLock(ScreenLockMonitor* monitor) {
    auto watch = create(); if (!watch) return nullptr;
    watch->lock = monitor;
    // Read only this client's logged-in session; no service/pre-login behavior.
    watchSession(watch);
    return watch;
}
void deskportUnwatchLifecycle(void* context) {
    auto watch = static_cast<Watch*>(context); if (!watch) return;
    if (watch->power) UnregisterSuspendResumeNotification(watch->power);
    if (watch->lock) WTSUnRegisterSessionNotification(watch->window);
    DestroyWindow(watch->window); delete watch;
}
