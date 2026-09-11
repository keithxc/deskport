#include <QtTest>
#include <QTemporaryDir>
#include "backend/sessionwindowstate.h"
#include "settings/streamingpreferences.h"
namespace WMUtils { bool isRunningWayland() { return false; } }
class DesktopState : public QObject {
    Q_OBJECT
private slots:
    void defaultsAndExplicitInputChoicesPersist() {
        QTemporaryDir directory;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QCoreApplication::setOrganizationName("DeskPortTest");
        QCoreApplication::setApplicationName("DesktopState");
        QSettings legacy;
        legacy.setValue("defaultver", 2);
        legacy.setValue("capturesyskeys", StreamingPreferences::CSK_OFF);
        legacy.setValue("sharedClipboard", false);
        auto prefs = StreamingPreferences::get();
        QVERIFY(prefs->sharedClipboard);
        QCOMPARE(prefs->captureSysKeysMode, StreamingPreferences::CSK_ALWAYS);
        QCOMPARE(QSettings().value("defaultver").toInt(), 3);
        QVERIFY(QSettings().value("sharedClipboard").toBool());
        QCOMPARE(QSettings().value("capturesyskeys").toInt(), int(StreamingPreferences::CSK_ALWAYS));
        QVERIFY(prefs->playAudioOnHost);
        prefs->playAudioOnHost = false;
        prefs->save(); prefs->reload();
        QVERIFY(!prefs->playAudioOnHost);
        QVERIFY(prefs->absoluteMouseMode);
        QVERIFY(prefs->showLocalCursor);
        QCOMPARE(prefs->captureSysKeysMode, StreamingPreferences::CSK_ALWAYS);
        prefs->captureSysKeysMode = StreamingPreferences::CSK_OFF;
        prefs->showLocalCursor = false;
        prefs->save(); prefs->reload();
        QCOMPARE(prefs->captureSysKeysMode, StreamingPreferences::CSK_OFF);
        QVERIFY(!prefs->showLocalCursor);
        prefs->captureSysKeysMode = StreamingPreferences::CSK_ALWAYS;
        prefs->showLocalCursor = true;
        prefs->save(); prefs->reload();
        QCOMPARE(prefs->captureSysKeysMode, StreamingPreferences::CSK_ALWAYS);
        QVERIFY(prefs->showLocalCursor);
    }
    void savedWorkspaceIsHostAndDisplaySpecific() {
        QTemporaryDir dir;
        const auto path = dir.path() + "/window.ini";
        const DeskPortDisplay::SessionWindowState state {QRect(50, 70, 1536, 864), QSize(1536, 864), 1, true, false};
        {
            QSettings settings(path, QSettings::IniFormat);
            DeskPortDisplay::writeSessionWindow(settings, "host-a", "outputs-150", 0, state);
            settings.sync();
        }
        QSettings settings(path, QSettings::IniFormat);
        auto restored = DeskPortDisplay::readSessionWindow(settings, "host-a", "outputs-150", 0);
        QVERIFY(restored.valid()); QCOMPARE(restored.geometry, state.geometry);
        QCOMPARE(restored.streamSize, state.streamSize); QVERIFY(restored.maximized);
        QVERIFY(!DeskPortDisplay::readSessionWindow(settings, "host-b", "outputs-150", 0).valid());
        QVERIFY(!DeskPortDisplay::readSessionWindow(settings, "host-a", "outputs-200", 0).valid());
        QVERIFY(!DeskPortDisplay::readSessionWindow(settings, "host-a", "outputs-150", 1).valid());
        auto invalid = state; invalid.streamSize = QSize(999999, 864);
        DeskPortDisplay::writeSessionWindow(settings, "host-a", "outputs-150", 0, invalid);
        QCOMPARE(DeskPortDisplay::readSessionWindow(settings, "host-a", "outputs-150", 0).streamSize, state.streamSize);
        settings.setValue(DeskPortDisplay::sessionWindowKey("host-a") + "/scale", 2);
        QVERIFY(!DeskPortDisplay::readSessionWindow(settings, "host-a", "outputs-150", 0).valid());
    }

};
QTEST_GUILESS_MAIN(DesktopState)
#include "desktop-state.moc"
