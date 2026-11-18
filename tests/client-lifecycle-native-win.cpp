#include <windows.h>
#include <wtsapi32.h>
namespace {
void inject(UINT message, WPARAM value) {
    HWND window = nullptr;
    while ((window = FindWindowExW(HWND_MESSAGE, window, L"DeskPortClientLifecycle", nullptr)))
        SendMessageW(window, message, value, 0);
}
}
void injectNativeSleep(bool sleeping) { inject(WM_POWERBROADCAST, sleeping ? PBT_APMSUSPEND : PBT_APMRESUMEAUTOMATIC); }
void injectNativeLock() { inject(WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK); }
