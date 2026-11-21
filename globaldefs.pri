# Support debug and release builds from command line for CI
CONFIG += debug_and_release

# Avoid noisy SDK version mismatch warnings on newer macOS SDKs
macx: CONFIG += sdk_no_version_check

# Ensure symbols are always generated
CONFIG += force_debug_info

# Disable asserts on release builds
CONFIG(release, debug|release) {
    DEFINES += NDEBUG
}

# Enable ASan for Linux or macOS
#CONFIG += sanitizer sanitize_address

# Enable ASan for Windows
#QMAKE_CFLAGS += -fsanitize=address
#QMAKE_CXXFLAGS += -fsanitize=address
#QMAKE_LFLAGS += -incremental:no -wholearchive:clang_rt.asan_dynamic-x86_64.lib -wholearchive:clang_rt.asan_dynamic_runtime_thunk-x86_64.lib

# MinGW cross builds on a case-sensitive filesystem need forwarding headers for
# the mixed-case Windows SDK spellings used by the sources.
win32-g++ {
    DESKPORT_WIN_COMPAT_INCLUDE = $$(DESKPORT_WIN_COMPAT_INCLUDE)
    !isEmpty(DESKPORT_WIN_COMPAT_INCLUDE): INCLUDEPATH += $$DESKPORT_WIN_COMPAT_INCLUDE
}
