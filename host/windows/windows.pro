QT = core
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = deskport-display
SOURCES += display-helper.cpp
LIBS += -luser32 -lsetupapi -luuid -lshell32 -lole32 -loleaut32 -luiautomationcore
win32-g++: QMAKE_LFLAGS += -static
