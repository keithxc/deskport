QT += core gui waylandcompositor
# Qt 6.5+ splits XdgShell into its own module; Qt 6.4 (Ubuntu 24.04) keeps it
# in waylandcompositor with the same QtWaylandCompositor headers.
qtHaveModule(waylandcompositorxdgshell): QT += waylandcompositorxdgshell

CONFIG += console c++17
CONFIG -= app_bundle

!linux: error(deskport-seamless-host is supported only on Linux)

TARGET = deskport-seamless-host

SOURCES += \
    main.cpp \
    seamlesshost.cpp \
    streamchannel.cpp

HEADERS += seamlesshost.h streamchannel.h ../../../app/seamless/seamlessstream.h

target.path = $$PREFIX/libexec
INSTALLS += target
