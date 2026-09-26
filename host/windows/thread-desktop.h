#pragma once
#include <windows.h>
#include <array>
#include <cwchar>

namespace deskport {
// GetThreadDesktop handles are borrowed. OpenInputDesktop handles are owned,
// but CloseDesktop cannot close one while this thread is still attached to it.
class thread_desktop {
  HDESK original = GetThreadDesktop(GetCurrentThreadId());
  HDESK owned = nullptr;
public:
  thread_desktop() = default;
  thread_desktop(const thread_desktop&) = delete;
  thread_desktop& operator=(const thread_desktop&) = delete;
  ~thread_desktop() {
    if (owned && SetThreadDesktop(original)) CloseDesktop(owned);
  }
  HDESK sync() {
    const auto next = OpenInputDesktop(DF_ALLOWOTHERACCOUNTHOOK, FALSE, GENERIC_ALL);
    if (!next) return nullptr;
    const auto current = GetThreadDesktop(GetCurrentThreadId());
    std::array<wchar_t, 256> current_name{}, next_name{};
    DWORD needed = 0;
    if (GetUserObjectInformationW(current, UOI_NAME, current_name.data(), sizeof(current_name), &needed) &&
        GetUserObjectInformationW(next, UOI_NAME, next_name.data(), sizeof(next_name), &needed) &&
        std::wcscmp(current_name.data(), next_name.data()) == 0) {
      CloseDesktop(next);
      return current;
    }
    if (!SetThreadDesktop(next)) {
      const auto error = GetLastError();
      CloseDesktop(next);
      SetLastError(error);
      return nullptr;
    }
    const auto previous = owned;
    owned = next;
    if (previous) CloseDesktop(previous);
    return owned;
  }
};
}
