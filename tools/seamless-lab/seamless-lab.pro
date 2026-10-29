# Standalone Seamless debug client. Independent of the DeskPort application:
# separate target, bundle identifier and settings domain.
QT += core gui widgets
CONFIG += c++17
CONFIG -= console

TARGET = SeamlessLab
VERSION = 0.1.0

SEAMLESS_DIR = $$PWD/../../app/seamless
INCLUDEPATH += $$SEAMLESS_DIR

SOURCES += \
    main.cpp \
    $$SEAMLESS_DIR/seamlessclientsession.cpp \
    $$SEAMLESS_DIR/seamlesskeymap.cpp \
    $$SEAMLESS_DIR/seamlesswindow.cpp

HEADERS += \
    $$SEAMLESS_DIR/seamlessclientsession.h \
    $$SEAMLESS_DIR/seamlesskeymap.h \
    $$SEAMLESS_DIR/seamlessstream.h \
    $$SEAMLESS_DIR/seamlesswindow.h

macx {
    QMAKE_TARGET_BUNDLE_PREFIX = io.github.keithxc
    QMAKE_BUNDLE = SeamlessLab
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 13.0
}

unix:!macx {
    target.path = $$PREFIX/bin
    INSTALLS += target
}
