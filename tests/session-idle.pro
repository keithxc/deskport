QT = core network
CONFIG += console c++17
CONFIG -= app_bundle debug_and_release
QMAKE_CXXFLAGS += -Wall -Wextra -UNDEBUG
TARGET = session-idle
SOURCES += $$PWD/session-idle.cpp $$PWD/../app/streaming/canvasbridge.cpp
INCLUDEPATH += $$PWD/../app $$PWD/../moonlight-common-c/moonlight-common-c/src
macx {
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 26.0
    FRAMEWORKS = $$PWD/../libs/mac/Frameworks
    QMAKE_CXXFLAGS += -F$$FRAMEWORKS
    INCLUDEPATH += $$FRAMEWORKS/SDL2.framework/Versions/A/Headers
    LIBS += -F$$FRAMEWORKS -framework SDL2
    QMAKE_RPATHDIR += $$FRAMEWORKS
} else {
    CONFIG += link_pkgconfig
    PKGCONFIG += sdl2
}
