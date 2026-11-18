#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGlyphRun>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSValue>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickGraphicsConfiguration>
#include <QRawFont>
#include <QSGRendererInterface>
#include <QSettings>
#include <QStyleHints>
#include <QScreen>
#include <QTextStream>
#include <QTextLayout>
#include <cmath>
#include <functional>
#include <QtTest>
#include "settings/streamingpreferences.h"
#include "gui/manual.h"

namespace WMUtils { bool isRunningWayland() { return false; } }

// No host, sockets, media, permission API or input injector is linked here.
// Only production QML, preferences, compiled translations and manual are used.
static QQuickItem* named(QQuickItem* item, const QString& name, bool visibleOnly = false) {
    if (item->objectName() == name && (!visibleOnly || item->isVisible())) return item;
    for (auto child : item->childItems()) if (auto found = named(child, name, visibleOnly)) return found;
    return nullptr;
}

static QJsonArray auditGlyphs(QQuickItem* root, int& missingGlyphs) {
    QJsonArray glyphs;
    std::function<void(QQuickItem*)> audit = [&](QQuickItem* text) {
        const auto value = text->property("text");
        if (text->isVisible() && value.isValid() && text->property("font").isValid() && !value.toString().isEmpty()) {
            QTextLayout layout(value.toString(), qvariant_cast<QFont>(text->property("font")));
            layout.beginLayout();
            while (true) { auto line = layout.createLine(); if (!line.isValid()) break; line.setLineWidth(10000); }
            layout.endLayout();
            for (const auto& run : layout.glyphRuns()) {
                int missing = 0; for (auto glyph : run.glyphIndexes()) if (!glyph) ++missing;
                missingGlyphs += missing;
                glyphs.append(QJsonObject{{"text", value.toString()}, {"font", run.rawFont().familyName()}, {"missing", missing}});
            }
        }
        for (auto child : text->childItems()) audit(child);
    };
    audit(root);
    return glyphs;
}

class DesktopTypography : public QObject {
    Q_OBJECT
private slots:
    void modalFocus() {
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& errors) {
            for (const auto& error : errors) warnings.append(error.toString());
        });
        QQmlComponent fixture(&engine);
        fixture.setData(R"(import QtQuick 2.9
import QtQuick.Controls 2.5
ApplicationWindow {
 width: 640; height: 640; visible: false
 NavigableDialog {
  id: form; objectName: "form"; title: "Add a legacy host"
  standardButtons: Dialog.Ok | Dialog.Cancel
  contentItem: Column {
   TextField { objectName: "firstInput"; placeholderText: "Host"; activeFocusOnTab: true }
   TextField { objectName: "disabledInput"; enabled: false; activeFocusOnTab: true }
   TextField { objectName: "hiddenInput"; visible: false; activeFocusOnTab: true }
   Item { width: 240; height: 48
    TextField { objectName: "secondInput"; placeholderText: "Port"; activeFocusOnTab: true }
   }
  }
 }
 NavigableMessageDialog {
  objectName: "message"; title: "Message"; text: "Offline keyboard regression"
  standardButtons: Dialog.Ok | Dialog.Cancel
 }
 UiTheme { id: ui }
 UiOptionPanel { objectName: "options"; title: "Options"; model: ["one", "two"] }
})", QUrl::fromLocalFile(qEnvironmentVariable("TEST_GUI_DIR") + "/focus-regression.qml"));
        QScopedPointer<QObject> root(fixture.create());
        QVERIFY2(root, qPrintable(fixture.errorString()));
        auto window = qobject_cast<QQuickWindow*>(root.data()); QVERIFY(window);
        window->show(); window->requestActivate(); QTest::qWait(50);
        auto form = root->findChild<QObject*>("form"); QVERIFY(form);
        const auto originalBehavior = QGuiApplication::styleHints()->tabFocusBehavior();
        for (auto behavior : {Qt::TabFocusAllControls, Qt::TabFocusTextControls}) {
            QGuiApplication::styleHints()->setTabFocusBehavior(behavior);
            for (bool footer : {true, false}) {
                form->setProperty("standardButtons", footer ? 0x00400400 : 0); // Ok | Cancel
                QVERIFY(QMetaObject::invokeMethod(form, "open"));
                QTRY_VERIFY(form->property("opened").toBool());
                QVERIFY(!form->property("explicitFocusCycle").toBool());
                auto first = named(window->contentItem(), "firstInput", true); QVERIFY(first);
                auto second = named(window->contentItem(), "secondInput", true); QVERIFY(second);
                for (bool backwards : {false, true}) {
                    // Start at a footer/close button: this is the original trap.
                    auto start = named(window->contentItem(), footer ? "dialogAcceptButton" : "panelClose", true);
                    QVERIFY(start); start->forceActiveFocus();
                    QSet<QString> visited;
                    for (int i = 0; i < 24; ++i) {
                        QTest::keyClick(window, Qt::Key_Tab, backwards ? Qt::ShiftModifier : Qt::NoModifier);
                        QTest::qWait(1);
                        auto focus = window->activeFocusItem(); QVERIFY(focus);
                        visited.insert(focus->objectName());
                        QVERIFY(focus->objectName() != "hiddenInput" && focus->objectName() != "disabledInput");
                    }
                    QVERIFY2(visited.contains("firstInput") && visited.contains("secondInput"),
                             qPrintable(QStringList(visited.values()).join(", ")));
                    if (behavior == Qt::TabFocusAllControls) {
                        QVERIFY(visited.contains("panelClose"));
                        if (footer) {
                            QVERIFY(visited.contains("dialogAcceptButton"));
                            QVERIFY(visited.contains("dialogRejectButton"));
                        }
                    }
                }
                QVERIFY(QMetaObject::invokeMethod(form, "close"));
                QTRY_VERIFY(!form->property("visible").toBool());
            }
        }
        auto message = root->findChild<QObject*>("message"); QVERIFY(message);
        QVERIFY(QMetaObject::invokeMethod(message, "open"));
        QTRY_VERIFY(message->property("opened").toBool());
        auto label = named(window->contentItem(), "messageText", true); QVERIFY(label);
        auto close = named(window->contentItem(), "panelClose", true); QVERIFY(close);
        auto accept = named(window->contentItem(), "dialogAcceptButton", true); QVERIFY(accept);
        auto cancel = named(window->contentItem(), "dialogRejectButton", true); QVERIFY(cancel);
        // Explicit message cycle skips disabled/invisible actions in both directions.
        cancel->setEnabled(false); close->setVisible(false); label->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Tab); QCOMPARE(window->activeFocusItem(), accept);
        QTest::keyClick(window, Qt::Key_Tab); QCOMPARE(window->activeFocusItem(), label);
        QTest::keyClick(window, Qt::Key_Tab, Qt::ShiftModifier); QCOMPARE(window->activeFocusItem(), accept);
        QTest::keyClick(window, Qt::Key_Tab, Qt::ShiftModifier); QCOMPARE(window->activeFocusItem(), label);
        QVERIFY(QMetaObject::invokeMethod(message, "close"));
        QTRY_VERIFY(!message->property("visible").toBool());
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
        auto options = root->findChild<QObject*>("options"); QVERIFY(options);
        QVERIFY(QMetaObject::invokeMethod(options, "open"));
        QTRY_VERIFY(options->property("opened").toBool());
        auto list = named(window->contentItem(), "optionList", true); QVERIFY(list);
        auto optionClose = named(window->contentItem(), "panelClose", true); QVERIFY(optionClose);
        optionClose->forceActiveFocus();
        bool listVisited = false;
        for (int i = 0; i < 16; ++i) {
            QTest::keyClick(window, Qt::Key_Tab);
            for (auto focus = window->activeFocusItem(); focus; focus = focus->parentItem())
                if (focus == list) listVisited = true;
        }
        QVERIFY(listVisited);
        QVERIFY(QMetaObject::invokeMethod(options, "close"));
        QGuiApplication::styleHints()->setTabFocusBehavior(originalBehavior);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
    void matrix() {
        const int viewportHeight = qEnvironmentVariable("DESKPORT_TYPOGRAPHY_HEIGHT", "760").toInt();
        QVERIFY(viewportHeight >= 360 && viewportHeight <= 1080);
        QList<int> widths;
        for (const auto& value : qEnvironmentVariable("DESKPORT_TYPOGRAPHY_WIDTHS", "640,1120").split(',')) {
            const int width = value.toInt(); QVERIFY(width >= 320 && width <= 1920); widths.append(width);
        }
        QVERIFY(!widths.isEmpty());
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& errors) {
            for (const auto& error : errors) warnings.append(error.toString());
        });
        const QString gui = qEnvironmentVariable("TEST_GUI_DIR");
        QQmlComponent fixtures(&engine);
        fixtures.setData(R"(import QtQuick 2.9
QtObject {
 property string deviceName: "DeskPort QA · 123"
 property bool changing: false; property bool running: false; property bool available: false
 property string status: ""; property string readiness: "attention"
 property var permissions: []; property string displayWarning: ""
 property bool physicalDisplaySharing: false; property bool virtualDisplayActive: true
 property int displayWidth: 2560; property int displayHeight: 1440; property int displayScale: 2
 property int sharingWidth: 2560; property bool loginStartManaged: true
 property bool unattendedEnabled: false; property bool loginStart: false
 property string unattendedStatus: ""; property bool canPair: false; property int basePort: 48989
 property var peers: []; property bool clientOnly: true; property int port: 48990
 property bool enabled: false; property string bundlePath: ""
 function refreshPermissions() {} function save() {}
})", QUrl());
        QScopedPointer<QObject> inert(fixtures.create()); QVERIFY(inert);
        for (const auto name : {"hostManager", "peerManager", "diagnostics"})
            engine.rootContext()->setContextProperty(name, inert.data());
        QQmlComponent harness(&engine);
        harness.setData(R"(import QtQuick 2.9
import QtQuick.Controls 2.5
import StreamingPreferences 1.0
ApplicationWindow {
 id: window; width: 640; height: 760; visible: false; font.pixelSize: 14
 property bool clearOnBack: false
 LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
 LayoutMirroring.childrenInherit: true
 UiTheme { id: ui }
 color: ui.canvas
 function navigateTo(url, name) {}
 property alias content: area
 Item { id: area; anchors.fill: parent }
})", QUrl::fromLocalFile(gui + "/typography-harness.qml"));
        QScopedPointer<QObject> root(harness.create()); QVERIFY2(root, qPrintable(harness.errorString()));
        auto window = qobject_cast<QQuickWindow*>(root.data()); QVERIFY(window);
        QQuickGraphicsConfiguration graphics;
        graphics.setAutomaticPipelineCache(false);
        graphics.setPipelineCacheSaveFile(qEnvironmentVariable("DESKPORT_TYPOGRAPHY_OUTPUT") + "/pipeline.cache");
        window->setGraphicsConfiguration(graphics);
        window->setScreen(QGuiApplication::primaryScreen());
        window->setPosition(window->screen()->availableGeometry().topLeft() + QPoint(32, 32));
        window->setMinimumSize(QSize(widths.first(), viewportHeight));
        window->setMaximumSize(QSize(widths.first(), viewportHeight));
        window->resize(widths.first(), viewportHeight);
        window->show();
        QCOMPARE(window->devicePixelRatio(), qEnvironmentVariable("DESKPORT_TYPOGRAPHY_DPR").toDouble());
        auto area = root->property("content").value<QQuickItem*>(); QVERIFY(area);
        auto prefs = StreamingPreferences::get(&engine);
        const QList<StreamingPreferences::Language> languages = {
            StreamingPreferences::LANG_AR, StreamingPreferences::LANG_BO,
            StreamingPreferences::LANG_HI, StreamingPreferences::LANG_TH,
            StreamingPreferences::LANG_ZH_CN, StreamingPreferences::LANG_JA,
            StreamingPreferences::LANG_KO, StreamingPreferences::LANG_DE,
            StreamingPreferences::LANG_RU, StreamingPreferences::LANG_EN};
        QJsonArray results;
        int missingGlyphs = 0;
        for (auto language : languages) {
            prefs->language = language;
            QVERIFY(prefs->retranslate()); prefs->save();
            const auto code = prefs->manualLanguage();
            if (!qEnvironmentVariable("DESKPORT_TYPOGRAPHY_LOCALE").isEmpty()
                    && code != qEnvironmentVariable("DESKPORT_TYPOGRAPHY_LOCALE")) continue;
            inert->setProperty("permissions", QVariantList{
                QVariantMap{{"key", "screen"}, {"title", QCoreApplication::translate("HostManager", "Desktop capture")},
                    {"purpose", QCoreApplication::translate("HostManager", "Let a connected device see this desktop and hear its sound.")}, {"state", "allowed"}},
                QVariantMap{{"key", "input"}, {"title", QCoreApplication::translate("HostManager", "Keyboard & mouse")},
                    {"purpose", QCoreApplication::translate("HostManager", "Let a device you approve control this computer.")}, {"state", "needsSetup"}},
                QVariantMap{{"key", "microphone"}, {"title", QCoreApplication::translate("HostManager", "Audio input")},
                    {"purpose", QCoreApplication::translate("HostManager", "Needed only when your sharing audio path uses microphone access.")}, {"state", "onShare"}}});
            QCOMPARE(QGuiApplication::layoutDirection(), code == "ar" ? Qt::RightToLeft : Qt::LeftToRight);
            // A new process loads the real INI persistence, translator and direction.
            QProcess restart;
            restart.start(QCoreApplication::applicationFilePath(), {"--read-language", QString::number(language)});
            QVERIFY(restart.waitForFinished(15000));
            QCOMPARE(restart.exitStatus(), QProcess::NormalExit);
            QVERIFY2(restart.exitCode() == 0, restart.readAllStandardError().constData());
            for (int width : widths) {
                window->setMinimumSize(QSize(1, 1));
                window->setMaximumSize(QSize(width, viewportHeight));
                window->setMinimumSize(QSize(width, viewportHeight));
                window->resize(width, viewportHeight);
                QTRY_COMPARE(window->size(), QSize(width, viewportHeight));
                for (const QString pageName : {"SettingsHome", "HostView", "HelpView"}) {
                    QString selectedHeading;
                    if (pageName == "SettingsHome" && language != StreamingPreferences::LANG_EN) {
                        selectedHeading = QCoreApplication::translate("SettingsHome", "Make DeskPort your own.");
                        prefs->setProperty("language", QVariant::fromValue(StreamingPreferences::LANG_EN));
                        QVERIFY(prefs->retranslate());
                    }
                    QQmlComponent component(&engine, QUrl::fromLocalFile(gui + "/" + pageName + ".qml"));
                    QScopedPointer<QObject> page(component.create(QQmlEngine::contextForObject(area)));
                    QVERIFY2(page, qPrintable(component.errorString()));
                    auto item = qobject_cast<QQuickItem*>(page.data()); QVERIFY(item);
                    item->setParentItem(area); item->setSize(QSizeF(width, viewportHeight));
                    // Let the manual card's 160 ms size animation settle.
                    QTest::qWait(240);
                    if (pageName == "HelpView") {
                        auto arrow = named(item, "chapterChevron1"); QVERIFY(arrow);
                        QTRY_COMPARE(qRound(arrow->rotation()), code == "ar" ? 180 : 0);
                        auto icon = named(item, "chapterIcon0"); QVERIFY(icon);
                        auto step = named(item, "chapterStep0"); QVERIFY(step);
                        const auto inset = step->mapToScene(QPointF(step->width() / 2, 0)).x()
                            - icon->mapToScene(QPointF(icon->width() / 2, 0)).x();
                        QVERIFY(code == "ar" ? inset < -20 : inset > 20);
                    }
                    if (pageName == "SettingsHome" && language != StreamingPreferences::LANG_EN) {
                        // Keep the same page and exercise the real settings handler.
                        auto choice = page->findChild<QObject*>("languageChoice"); QVERIFY(choice);
                        const auto choices = choice->property("languages").value<QJSValue>().toVariant().toList();
                        int target = -1, english = -1;
                        for (int i = 0; i < choices.size(); ++i) {
                            const int value = choices[i].toMap().value("value").toInt();
                            if (value == language) target = i;
                            if (value == StreamingPreferences::LANG_EN) english = i;
                        }
                        QVERIFY(target >= 0 && english >= 0);
                        QVERIFY(QMetaObject::invokeMethod(choice, "activated", Q_ARG(int, english)));
                        QCOMPARE(page->property("heading").toString(), QString("Make DeskPort your own."));
                        QCOMPARE(QGuiApplication::layoutDirection(), Qt::LeftToRight);
                        QVERIFY(QMetaObject::invokeMethod(choice, "activated", Q_ARG(int, target)));
                        QCOMPARE(page->property("heading").toString(), selectedHeading);
                        QCOMPARE(QGuiApplication::layoutDirection(), code == "ar" ? Qt::RightToLeft : Qt::LeftToRight);
                        QTest::qWait(100);
                        auto chevron = named(qobject_cast<QQuickItem*>(choice), "rowChevron"); QVERIFY(chevron);
                        QTRY_COMPARE(qRound(chevron->rotation()), code == "ar" ? 180 : 0);
                    }
                    QJsonObject result{{"locale", code}, {"page", pageName}, {"width", width}, {"height", viewportHeight},
                        {"dpr", window->devicePixelRatio()}, {"direction", int(QGuiApplication::layoutDirection())}};
                    result["glyphs"] = auditGlyphs(item, missingGlyphs);
                    QJsonArray elisions;
                    std::function<void(QQuickItem*)> geometry = [&](QQuickItem* text) {
                        if (text->isVisible() && text->property("truncated").toBool())
                            elisions.append(text->property("text").toString());
                        for (auto child : text->childItems()) geometry(child);
                    };
                    geometry(item); result["elided"] = elisions;
                    const auto output = qEnvironmentVariable("DESKPORT_TYPOGRAPHY_OUTPUT");
                    QVERIFY(!output.isEmpty()); QDir().mkpath(output);
                    auto shot = [&](const QString& name) {
                        const double expectedDpr = qEnvironmentVariable("DESKPORT_TYPOGRAPHY_DPR").toDouble();
                        if (window->size() != QSize(width, viewportHeight)
                                || !qFuzzyCompare(window->devicePixelRatio(), expectedDpr)) {
                            qWarning() << "Capture geometry" << code << name << window->size() << window->devicePixelRatio()
                                       << "expected" << QSize(width, viewportHeight) << qEnvironmentVariable("DESKPORT_TYPOGRAPHY_DPR");
                            return false;
                        }
                        const auto path = output + "/" + code + "-" + QString::number(width) + "-" + name + ".png";
                        const QSize pixels(qRound(width * expectedDpr), qRound(viewportHeight * expectedDpr));
                        QImage frame;
                        // A new native swapchain can lag a resize or retranslation.
                        // Wait for the correct raster instead of saving a stale frame.
                        for (int attempt = 0; attempt < 10; ++attempt) {
                            frame = window->grabWindow();
                            if (frame.size() == pixels) return frame.save(path);
                            window->requestUpdate(); QTest::qWait(50);
                        }
                        qWarning() << "Capture unavailable" << code << name << frame.size() << "expected" << pixels
                                   << window->isVisible() << window->isExposed();
                        return false;
                    };
                    QVERIFY(shot(pageName));
                    auto flick = item->property("contentItem").value<QQuickItem*>(); QVERIFY(flick);
                    const double contentHeight = flick->property("contentHeight").toDouble();
                    result["scrollHeight"] = contentHeight;
                    if (contentHeight > flick->height()) {
                        flick->setProperty("contentY", contentHeight - flick->height());
                        QTest::qWait(40);
                        QVERIFY(shot(pageName + "-bottom"));
                        flick->setProperty("contentY", 0);
                        QTest::qWait(40);
                    }
                    if (pageName == "SettingsHome") {
                        auto choice = page->findChild<QObject*>("languageChoice"); QVERIFY(choice);
                        QVERIFY(QMetaObject::invokeMethod(choice, "clicked")); QTest::qWait(100);
                        auto panel = page->findChild<QObject*>("languageChoicePanel"); QVERIFY(panel);
                        QTRY_VERIFY(panel->property("opened").toBool());
                        auto list = named(window->contentItem(), "optionList"); QVERIFY(list);
                        QVERIFY(list->hasActiveFocus());
                        const int selected = list->property("currentIndex").toInt();
                        QVERIFY(selected >= 0);
                        QTest::keyClick(window, Qt::Key_Down);
                        QCOMPARE(list->property("currentIndex").toInt(), selected + 1);
                        QTest::keyClick(window, Qt::Key_Up);
                        QCOMPARE(list->property("currentIndex").toInt(), selected);
                        QVERIFY(shot("languages"));
                        result["languagePickerGlyphs"] = auditGlyphs(window->contentItem(), missingGlyphs);
                        QTest::keyClick(window, Qt::Key_Return);
                        QTRY_VERIFY(!panel->property("visible").toBool());
                        QCOMPARE(prefs->language, language);
                        auto notice = page->findChild<QObject*>("feedbackNotice"); QVERIFY(notice);
                        QVERIFY(QMetaObject::invokeMethod(notice, "open"));
                        QTRY_VERIFY(notice->property("opened").toBool());
                        auto cancel = named(window->contentItem(), "dialogRejectButton", true); QVERIFY(cancel);
                        QCOMPARE(cancel->property("text").toString(), QCoreApplication::translate("QPlatformTheme", "Cancel"));
                        auto accept = named(window->contentItem(), "dialogAcceptButton", true); QVERIFY(accept);
                        QCOMPARE(accept->property("text").toString(), notice->property("acceptText").toString());
                        QVERIFY(shot("confirmation"));
                        result["confirmationGlyphs"] = auditGlyphs(window->contentItem(), missingGlyphs);
                        bool buttonFocused = false;
                        QStringList focusPath;
                        for (int i = 0; i < 6; ++i) {
                            QTest::keyClick(window, Qt::Key_Tab);
                            auto focus = window->activeFocusItem();
                            focusPath.append(focus ? QString::fromLatin1(focus->metaObject()->className()) + ":" + focus->objectName() : "none");
                            if (focus && (focus->objectName() == "dialogAcceptButton" || focus->objectName() == "dialogRejectButton")) buttonFocused = true;
                        }
                        QVERIFY2(buttonFocused, qPrintable(focusPath.join(", ")));
                        auto beforeBacktab = window->activeFocusItem();
                        QTest::keyClick(window, Qt::Key_Backtab);
                        QVERIFY(window->activeFocusItem() != beforeBacktab);
                        QTest::keyClick(window, Qt::Key_Tab);
                        QCOMPARE(window->activeFocusItem(), beforeBacktab);
                        QTest::keyClick(window, Qt::Key_Escape); QTest::qWait(100);
                        QTRY_VERIFY(!notice->property("visible").toBool());
                    }
                    results.append(result);
                }
                QQmlComponent error(&engine);
                error.setData(R"(import QtQuick 2.9
import QtQuick.Controls 2.5
ErrorMessageDialog {
 title: qsTranslate("SettingsHome", "Report a problem")
 text: qsTranslate("StreamSegue", "Starting %1 failed: Error %2").arg("QA").arg(-1) + "\n\n" + qsTranslate("StreamSegue", "Check your firewall and port forwarding rules for port(s): %1").arg("49000, 49001")
})", QUrl::fromLocalFile(gui + "/typography-error.qml"));
                QScopedPointer<QObject> dialog(error.create(QQmlEngine::contextForObject(area)));
                QVERIFY2(dialog, qPrintable(error.errorString()));
                dialog->setProperty("parent", QVariant::fromValue(area));
                QVERIFY(QMetaObject::invokeMethod(dialog.data(), "open"));
                QTRY_VERIFY(dialog->property("opened").toBool());
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("DESKPORT_TYPOGRAPHY_OUTPUT") + "/" + code + "-" + QString::number(width) + "-error.png"));
                results.append(QJsonObject{{"locale", code}, {"page", "ErrorMessageDialog"}, {"width", width}, {"height", viewportHeight},
                    {"dpr", window->devicePixelRatio()}, {"glyphs", auditGlyphs(window->contentItem(), missingGlyphs)}});
                const auto message = dialog->property("text").toString();
                dialog->setProperty("text", (message + "\n\n").repeated(16));
                QTest::qWait(100);
                auto scroll = dialog->findChild<QObject*>("messageScroll"); QVERIFY(scroll);
                auto content = scroll->property("contentItem").value<QQuickItem*>(); QVERIFY(content);
                QTRY_VERIFY2(content->property("contentHeight").toDouble() > content->height(),
                    qPrintable(QString("%1/%2 content=%3 viewport=%4 dialog=%5 max=%6 scroll=%7").arg(code).arg(width)
                        .arg(content->property("contentHeight").toDouble()).arg(content->height())
                        .arg(dialog->property("height").toDouble()).arg(dialog->property("maximumHeight").toDouble())
                        .arg(scroll->property("height").toDouble())));
                QTest::keyClick(window, Qt::Key_PageDown);
                QTRY_VERIFY(content->property("contentY").toDouble() > 0);
                QTest::keyClick(window, Qt::Key_Home);
                QTRY_COMPARE(content->property("contentY").toDouble(), 0.0);
                QTest::keyClick(window, Qt::Key_End);
                QTRY_VERIFY(std::abs(content->property("contentY").toDouble()
                    - (content->property("contentHeight").toDouble() - content->height())) < 1.0);
                QTest::qWait(30);
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("DESKPORT_TYPOGRAPHY_OUTPUT") + "/" + code + "-" + QString::number(width) + "-error-scrolled.png"));
                QTest::keyClick(window, Qt::Key_Return); QTest::qWait(100);
                QTRY_VERIFY(!dialog->property("visible").toBool());
                // Rebuilding the footer must not break ErrorMessageDialog's
                // standardButtons expression when browser availability changes.
                auto system = engine.singletonInstance<QObject*>(qmlTypeId("SystemProperties", 1, 0, "SystemProperties"));
                QVERIFY(system);
                const int buttons = dialog->property("standardButtons").toInt();
                system->setProperty("hasBrowser", true);
                QTRY_VERIFY(dialog->property("standardButtons").toInt() != buttons);
                system->setProperty("hasBrowser", false);
                QTRY_COMPARE(dialog->property("standardButtons").toInt(), buttons);
            }
        }
        const double expectedDpr = qEnvironmentVariable("DESKPORT_TYPOGRAPHY_DPR").toDouble();
        for (const auto& result : results) QCOMPARE(result.toObject()["dpr"].toDouble(), expectedDpr);
        const QDir captures(qEnvironmentVariable("DESKPORT_TYPOGRAPHY_OUTPUT"));
        for (const auto& name : captures.entryList({"*.png"}, QDir::Files)) {
            bool matched = false;
            for (int width : widths) if (name.contains("-" + QString::number(width) + "-")) {
                QImageReader reader(captures.filePath(name));
                QCOMPARE(reader.size(), QSize(qRound(width * expectedDpr), qRound(viewportHeight * expectedDpr)));
                matched = true; break;
            }
            QVERIFY(matched);
        }
        QJsonObject report{{"platform", QGuiApplication::platformName()}, {"qt", qVersion()},
            {"system", QSysInfo::prettyProductName()}, {"graphicsApi", int(window->rendererInterface()->graphicsApi())}, {"results", results}};
        QFile file(qEnvironmentVariable("DESKPORT_TYPOGRAPHY_OUTPUT") + "/report.json");
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(report).toJson());
        QCOMPARE(missingGlyphs, 0);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (app.arguments().contains("--print-dpr")) {
        QTextStream(stdout) << app.primaryScreen()->devicePixelRatio();
        return 0;
    }
    if (!QDir::isAbsolutePath(qEnvironmentVariable("DESKPORT_TYPOGRAPHY_SETTINGS"))) return 2;
    QCoreApplication::setOrganizationName("DeskPortTypographyTests");
    QCoreApplication::setApplicationName("IsolatedFixture");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, qEnvironmentVariable("DESKPORT_TYPOGRAPHY_SETTINGS"));
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, qEnvironmentVariable("DESKPORT_TYPOGRAPHY_SETTINGS"));
    if (app.arguments().contains("--read-language")) {
        const int expected = app.arguments().last().toInt();
        auto prefs = StreamingPreferences::get();
        return int(prefs->language) == expected && prefs->retranslate()
            && QGuiApplication::layoutDirection() == (expected == StreamingPreferences::LANG_AR ? Qt::RightToLeft : Qt::LeftToRight) ? 0 : 1;
    }
    qmlRegisterSingletonType<StreamingPreferences>("StreamingPreferences", 1, 0, "StreamingPreferences",
        +[](QQmlEngine* engine, QJSEngine*) -> QObject* {
            auto prefs = StreamingPreferences::get(engine); QQmlEngine::setObjectOwnership(prefs, QQmlEngine::CppOwnership); return prefs;
        });
    qmlRegisterSingletonType<Manual>("Manual", 1, 0, "Manual", +[](QQmlEngine*, QJSEngine*) -> QObject* { return new Manual; });
    qmlRegisterSingletonType<QObject>("SystemProperties", 1, 0, "SystemProperties", +[](QQmlEngine* engine, QJSEngine*) -> QObject* {
        QQmlComponent c(engine); c.setData("import QtQuick 2.9; QtObject { property string versionString: \"QA fixture\"; property bool hasBrowser: false }", QUrl()); return c.create();
    });
    DesktopTypography tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "desktop-typography.moc"
