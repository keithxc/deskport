QT += core gui widgets network testlib
linux: QT += dbus
CONFIG += console c++17 testcase link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += openssl
TARGET = backend-timers
tray_icon.prefix = /res
tray_icon.base = $$PWD/../app/res
tray_icon.files = $$PWD/../app/res/deskport-tray-black.svg
RESOURCES += tray_icon
INCLUDEPATH += $$PWD/../app $$PWD/../app/backend $$PWD/../moonlight-common-c/moonlight-common-c/src
SOURCES += $$PWD/backend-timers.cpp \
    $$PWD/../app/backend/browsergateway.cpp $$PWD/../app/backend/browserhost.cpp \
    $$PWD/../app/backend/peermanager.cpp $$PWD/../app/backend/peermanager-session.cpp $$PWD/../app/backend/peermanager-devices.cpp \
    $$PWD/../app/backend/hostmanager.cpp $$PWD/../app/backend/hostmanager-startup.cpp \
    $$PWD/../app/backend/hostmanager-trust.cpp $$PWD/../app/backend/hostmanager-display.cpp \
    $$PWD/../app/backend/diagnostics.cpp $$PWD/../app/backend/nvaddress.cpp \
    $$PWD/../third_party/qrcodegen/qrcodegen.cpp
HEADERS += $$PWD/../app/backend/browsergateway.h $$PWD/../app/backend/browserhost.h \
    $$PWD/../app/backend/peermanager.h $$PWD/../app/backend/hostmanager.h $$PWD/../app/backend/diagnostics.h
macx {
    OBJECTIVE_SOURCES += $$PWD/../app/backend/macpermissions.mm $$PWD/../app/backend/macdock.mm \
        $$PWD/../app/backend/macclipboard.mm $$PWD/../app/backend/macunattended.mm
    LIBS += -framework CoreGraphics -framework AVFoundation -framework ApplicationServices -framework AppKit -framework ServiceManagement
}
