// Run on an interactive Windows desktop; never switches the visible desktop.
#include "host/windows/thread-desktop.h"
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cassert>

static void cycle(bool alternate) {
  std::thread([alternate] {
    auto original = GetThreadDesktop(GetCurrentThreadId());
    auto other = alternate ? CreateDesktopW(L"DeskPortLifetimeTest", nullptr, nullptr, 0, GENERIC_ALL, nullptr) : nullptr;
    if (alternate && (!other || !SetThreadDesktop(other))) std::abort();
    {
      deskport::thread_desktop desktop;
      for (int i=0; i<10; ++i) if (!desktop.sync()) std::abort();
    }
    if (other) {
      if (!SetThreadDesktop(original) || !CloseDesktop(other)) std::abort();
    }
  }).join();
}
int main(int argc,char** argv) {
  if(argc>1) std::freopen(argv[1], "w", stdout);
  cycle(false); cycle(true);
  DWORD before=0, after=0;
  GetProcessHandleCount(GetCurrentProcess(), &before);
  for(int i=0;i<100;++i) { cycle(false); cycle(true); }
  GetProcessHandleCount(GetCurrentProcess(), &after);
  std::printf("200 thread lifetimes / 2000 desktop syncs: handles before=%lu after=%lu\n",before,after);
  return after>before+2 ? 1 : 0;
}
