#include <QtTest>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTranslator>
#include <functional>
#include "peermanager.h"
#include "singleinstance.h"

static QByteArray credential(const char* name) {
    QFile f(qEnvironmentVariable(name)); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
class TestSession : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    int executions = 0;
    QQuickWindow* receivedWindow = nullptr;
    TestSession* next = nullptr;
    std::function<void()> duringExec;
    Q_INVOKABLE bool adaptiveRestartPending() const { return next != nullptr; }
    Q_INVOKABLE TestSession* adaptiveContinuation() { auto value = next; next = nullptr; return value; }
    Q_INVOKABLE void exec(QQuickWindow* window) { receivedWindow = window; ++executions; if (duringExec) duringExec(); }
signals:
    void stageStarting(QString stage);
    void stageFailed(QString stage, int error, QString ports);
    void connectionStarted();
    void displayLaunchError(QString text);
    void displayLaunchWarning(QString text);
    void quitStarting();
    void sessionFinished(int result);
    void readyForDeletion();
};
static QString desktopTestState;
static TestSession* desktopTestSession;
static int desktopCreateCalls;
class TestDesktopApps : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE void initialize(QObject*, int, bool) {}
    Q_INVOKABLE QVariantMap desktopTarget() const {
        return {{"state", desktopTestState}, {"index", 0}, {"name", "Desktop"}, {"resume", false}};
    }
    Q_INVOKABLE TestSession* createSessionForApp(int) { ++desktopCreateCalls; return desktopTestSession; }
signals:
    void computerLost();
};
class TestPreferences : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    enum Language { LANG_AUTO, LANG_EN, LANG_FR, LANG_ZH_CN, LANG_DE, LANG_NB_NO, LANG_RU, LANG_ES, LANG_JA, LANG_VI, LANG_TH, LANG_KO, LANG_HU, LANG_NL, LANG_SV, LANG_TR, LANG_UK, LANG_ZH_TW, LANG_PT, LANG_PT_BR, LANG_EL, LANG_IT, LANG_HI, LANG_PL, LANG_CS, LANG_HE, LANG_CKB, LANG_LT, LANG_ET };
    Q_ENUM(Language)
};
class UiPages : public QObject {
    Q_OBJECT
private slots:
    void repeatedLaunchActivatesOnlyTheOwner() {
        QTemporaryDir directory;
        int activations = 0;
        {
            SingleInstance owner;
            owner.activate = [&] { ++activations; };
            QVERIFY(owner.start(directory.path()));
            for (int i = 0; i < 3; ++i) {
                SingleInstance duplicate;
                QVERIFY(!duplicate.start(directory.path()));
                QVERIFY(duplicate.delivered);
                QTRY_COMPARE(activations, i + 1);
            }
            SingleInstance background;
            QVERIFY(!background.start(directory.path(), false));
            QVERIFY(background.delivered);
            QTest::qWait(50);
            QCOMPARE(activations, 3);
        }
        SingleInstance restarted;
        QVERIFY(restarted.start(directory.path()));
    }
    void initTestCase() {
        qmlRegisterType<TestSession>("Session",1,0,"Session");
        qmlRegisterType<TestDesktopApps>("AppModel",1,0,"AppModel");
        qmlRegisterSingletonType<QObject>("SdlGamepadKeyNavigation",1,0,"SdlGamepadKeyNavigation",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine); c.setData("import QtQuick 2.9; QtObject { function enable() {} function disable() {} function getConnectedGamepads() { return 0 } }",QUrl()); return c.create();
        });
        qmlRegisterSingletonType<QObject>("ComputerManager",1,0,"ComputerManager",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine); c.setData("import QtQuick 2.9; QtObject { signal quitAppCompleted(var error) }",QUrl()); return c.create();
        });
        qmlRegisterType<TestPreferences>("TestPreferences",1,0,"TestPreferences");
        qmlRegisterSingletonType<TestPreferences>("StreamingPreferences",1,0,"StreamingPreferences",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine);
            c.setData(R"(import QtQuick 2.9
import TestPreferences 1.0
TestPreferences {
 property int language: 1; property int retranslations: 0
 function retranslate() { retranslations++; return true }
 property int width: 2048; property int height: 1152; property int fps: 75; property int bitrateKbps: 125000
 property int windowMode: 2; property int captureSysKeysMode: 1; property int saves: 0
 property bool sharedClipboard: false; property bool showLocalCursor: true; property bool adaptiveResolution: true; property bool enableVsync: true; property bool absoluteMouseMode: true; property bool reverseScrollDirection: false
 property bool muteOnFocusLoss: true; property bool playAudioOnHost: false; property bool enableMdns: true; property bool keepAwake: true
 function save() { saves++ }
})",QUrl()); return c.create();
        });
        qmlRegisterSingletonType<QObject>("SystemProperties",1,0,"SystemProperties",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine); c.setData("import QtQuick 2.9; QtObject { property bool hasBrowser: false }",QUrl()); return c.create();
        });
    }
    void continuationDuringNestedEventLoop() {
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        TestSession first, next;
        QQmlEngine::setObjectOwnership(&first, QQmlEngine::CppOwnership);
        QQmlEngine::setObjectOwnership(&next, QQmlEngine::CppOwnership);
        engine.rootContext()->setContextProperty("testSession", &first);
        first.duringExec = [&] {
            first.next = &next;
            emit first.sessionFinished(0);
            QTimer::singleShot(0, &first, [&] { emit first.readyForDeletion(); });
            QTest::qWait(100);
        };
        QQmlComponent harness(&engine);
        harness.setData(R"(import QtQuick 2.9
import QtQuick.Controls 2.2
ApplicationWindow {
 id: window; width: 800; height: 600
 StackView { id: stackView; anchors.fill: parent; initialItem: Item {} }
 function start() { stackView.push(Qt.resolvedUrl("StreamSegue.qml"), {session: testSession, appName: "Test"}, StackView.Immediate) }
})",QUrl::fromLocalFile(qEnvironmentVariable("TEST_GUI_DIR")+"/nested-harness.qml"));
        QScopedPointer<QObject> root(harness.create()); QVERIFY2(root,qPrintable(harness.errorString()));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"start"));
        QTRY_COMPARE(next.executions,1);
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
    }
    void continuationAfterDeferredCleanup() {
        // Real sessions return from exec() first; readyForDeletion arrives
        // later from the cleanup worker. The continuation must still start.
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        TestSession first, next;
        QQmlEngine::setObjectOwnership(&first, QQmlEngine::CppOwnership);
        QQmlEngine::setObjectOwnership(&next, QQmlEngine::CppOwnership);
        engine.rootContext()->setContextProperty("testSession", &first);
        first.duringExec = [&] {
            first.next = &next;
            emit first.sessionFinished(0);
            QTimer::singleShot(50, &first, [&] { emit first.readyForDeletion(); });
        };
        QQmlComponent harness(&engine);
        harness.setData(R"(import QtQuick 2.9
import QtQuick.Controls 2.2
ApplicationWindow {
 id: window; width: 800; height: 600
 StackView { id: stackView; anchors.fill: parent; initialItem: Item {} }
 function start() { stackView.push(Qt.resolvedUrl("StreamSegue.qml"), {session: testSession, appName: "Test"}, StackView.Immediate) }
})",QUrl::fromLocalFile(qEnvironmentVariable("TEST_GUI_DIR")+"/deferred-harness.qml"));
        QScopedPointer<QObject> root(harness.create()); QVERIFY2(root,qPrintable(harness.errorString()));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"start"));
        QTRY_COMPARE(next.executions,1);
        QCOMPARE(next.receivedWindow, qobject_cast<QQuickWindow*>(root.data()));
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
    }
    void streamAndQuitActivateWithoutLegacyToolbar() {
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        TestSession session;
        engine.rootContext()->setContextProperty("testSession", &session);
        // No toolBar in this context: exercise activation, not just page creation.
        QQmlComponent harness(&engine);
        harness.setData(R"(import QtQuick 2.9
import QtQuick.Controls 2.2
ApplicationWindow {
 id: window; width: 800; height: 600
 property int quits: 0
 property bool navigationVisible: !stackView.currentItem || stackView.currentItem.hidesNavigation !== true
 property alias currentPage: stackView.currentItem
 StackView { id: stackView; anchors.fill: parent; initialItem: Item {} }
 function startStream() { stackView.push(Qt.resolvedUrl("StreamSegue.qml"), { session: testSession, appName: "Test" }, StackView.Immediate) }
 function startQuit() { stackView.push(Qt.resolvedUrl("QuitSegue.qml"), { appName: "Test", quitRunningAppFn: function() { window.quits++ } }, StackView.Immediate) }
 function back() { stackView.pop(StackView.Immediate) }
})",QUrl::fromLocalFile(qEnvironmentVariable("TEST_GUI_DIR")+"/test-harness.qml"));
        QScopedPointer<QObject> root(harness.create()); QVERIFY2(root,qPrintable(harness.errorString()));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"startStream"));
        QTRY_COMPARE(session.executions,1);
        QVERIFY(!root->property("navigationVisible").toBool());
        emit session.stageStarting("Handshake");
        auto page=root->property("currentPage").value<QObject*>(); QVERIFY(page);
        QCOMPARE(page->property("stageText").toString(),QString("Starting Handshake..."));
        TestSession continuation;
        QQmlEngine::setObjectOwnership(&continuation, QQmlEngine::CppOwnership);
        session.next = &continuation;
        emit session.sessionFinished(0);
        QCOMPARE(root->property("currentPage").value<QObject*>(), page);
        emit session.readyForDeletion();
        QTRY_COMPARE(continuation.executions,1);
        QVERIFY(!root->property("navigationVisible").toBool());
        QVERIFY(root->property("currentPage").value<QObject*>() != page);
        QVERIFY(QMetaObject::invokeMethod(root.data(),"back"));
        QVERIFY(root->property("navigationVisible").toBool());
        QVERIFY(QMetaObject::invokeMethod(root.data(),"startQuit"));
        QTRY_COMPARE(root->property("quits").toInt(),1);
        QVERIFY(!root->property("navigationVisible").toBool());
        QVERIFY(QMetaObject::invokeMethod(root.data(),"back"));
        QVERIFY(root->property("navigationVisible").toBool());
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
    }
    void controlCenterDoesNotReplaceActiveSession() {
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        TestSession session;
        engine.rootContext()->setContextProperty("testSession", &session);
        QQmlComponent harness(&engine);
        harness.setData(R"(import QtQuick 2.9
import QtQuick.Controls 2.2
ApplicationWindow {
 id: window; width: 800; height: 600
 property alias depth: stackView.depth
 property alias currentPage: stackView.currentItem
 QtObject { id: streamSegueErrorDialog; property string text: ""; property bool quitAfter: false; function open() {} }
 StackView { id: stackView; anchors.fill: parent; initialItem: Item {} }
 function start() { stackView.push(Qt.resolvedUrl("StreamSegue.qml"), {session: testSession, appName: "Test"}, StackView.Immediate) }
 function showDevices() { stackView.push(controlPage, StackView.Immediate) }
 Component { id: controlPage; Item { property bool controlCenterForActiveSession: true } }
})",QUrl::fromLocalFile(qEnvironmentVariable("TEST_GUI_DIR")+"/control-center-harness.qml"));
        QScopedPointer<QObject> root(harness.create()); QVERIFY2(root,qPrintable(harness.errorString()));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"start"));
        QTRY_COMPARE(session.executions,1);
        QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevices"));
        QCOMPARE(session.receivedWindow, qobject_cast<QQuickWindow*>(root.data()));
        QCOMPARE(root->property("depth").toInt(),3);
        QVERIFY(root->property("currentPage").value<QObject*>()->property("controlCenterForActiveSession").toBool());
        emit session.sessionFinished(0);
        QTRY_COMPARE(root->property("depth").toInt(),1);
        QTest::qWait(50);
        engine.collectGarbage();
        emit session.readyForDeletion();
        QTest::qWait(20);
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
    }
    void desktopShortcut_data() {
        QTest::addColumn<QString>("state");
        for (const auto& value : {"ready", "waiting", "missing", "busy", "cancel"}) QTest::newRow(value) << QString(value);
    }
    void desktopShortcut() {
        QFETCH(QString, state);
        desktopTestState = state == "cancel" ? "waiting" : state; desktopCreateCalls = 0;
        QQmlEngine engine;
        TestSession session; desktopTestSession = &session;
        QQmlEngine::setObjectOwnership(&session, QQmlEngine::CppOwnership);
        QQmlComponent harness(&engine);
        harness.setData(R"(import QtQuick 2.9
import QtQuick.Controls 2.2
ApplicationWindow {
 id: window; width: 800; height: 600
 property alias currentPage: stackView.currentItem
 property alias depth: stackView.depth
 QtObject { id: streamSegueErrorDialog; property string text: ""; property bool quitAfter: false; function open() {} }
 StackView { id: stackView; anchors.fill: parent; initialItem: Item {} }
 function start() { stackView.push(Qt.resolvedUrl("DesktopSegue.qml"), {computerIndex: 0}, StackView.Immediate) }
 function back() { stackView.pop(StackView.Immediate) }
})",QUrl::fromLocalFile(qEnvironmentVariable("TEST_GUI_DIR")+"/desktop-harness.qml"));
        QScopedPointer<QObject> root(harness.create()); QVERIFY2(root,qPrintable(harness.errorString()));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"start"));
        if (state == "cancel") {
            QVERIFY(QMetaObject::invokeMethod(root.data(),"back"));
            desktopTestState = "ready";
            QTest::qWait(300);
            QCOMPARE(desktopCreateCalls,0);
            QCOMPARE(root->property("depth").toInt(),1);
            return;
        }
        if (state == "waiting") {
            QTest::qWait(250);
            QCOMPARE(desktopCreateCalls,0);
            desktopTestState = "ready";
        } else if (state != "ready") {
            QCOMPARE(desktopCreateCalls,0);
            auto page=root->property("currentPage").value<QObject*>(); QVERIFY(page);
            QTRY_VERIFY(!page->property("errorText").toString().isEmpty());
            QVERIFY(QMetaObject::invokeMethod(root.data(),"back"));
            QCOMPARE(root->property("depth").toInt(),1);
            return;
        }
        QTRY_COMPARE(session.executions,1);
        QCOMPARE(desktopCreateCalls,1);
        QCOMPARE(root->property("depth").toInt(),2);
        emit session.sessionFinished(0);
        QTRY_COMPARE(root->property("depth").toInt(),1);
        QTest::qWait(250);
        QCOMPARE(desktopCreateCalls,1);
    }
    void pagesLoadWithoutChangingAccessOrSettings() {
        QTemporaryDir dir;
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager peers(&host,credential("TEST_CERT_A"),credential("TEST_KEY_A"),dir.path()+"/peers",0,QHostAddress::LocalHost);
        QQmlEngine engine;
        const QString gui=qEnvironmentVariable("TEST_GUI_DIR");
        QQmlComponent themeComponent(&engine,QUrl::fromLocalFile(gui+"/UiTheme.qml"));
        QScopedPointer<QObject> theme(themeComponent.create()); QVERIFY(theme);
        engine.rootContext()->setContextProperty("ui",theme.data());
        engine.rootContext()->setContextProperty("hostManager",&host);
        engine.rootContext()->setContextProperty("peerManager",&peers);
        QQuickWindow window;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        QQmlComponent prefsAccess(&engine);
        prefsAccess.setData("import QtQuick 2.9; import StreamingPreferences 1.0; QtObject { property var prefs: StreamingPreferences }",QUrl());
        QScopedPointer<QObject> access(prefsAccess.create()); QVERIFY(access);
        auto prefs=access->property("prefs").value<QObject*>(); QVERIFY(prefs);
        for(const auto& name: {"SetupView","BindView","HostView","SettingsHome"}) {
            QQmlComponent component(&engine,QUrl::fromLocalFile(gui+"/"+name+".qml"));
            QVERIFY2(component.isReady(),qPrintable(component.errorString()));
            QScopedPointer<QObject> page(component.create()); QVERIFY2(page,qPrintable(component.errorString()));
            auto item=qobject_cast<QQuickItem*>(page.data()); QVERIFY(item);
            item->setParentItem(window.contentItem()); item->setWidth(780); item->setHeight(620);
            QTest::qWait(30);
            QVERIFY(page->property("contentHeight").toReal() > 0);
            QVERIFY(peers.peers().isEmpty()); QVERIFY(!peers.busy()); QVERIFY(!host.running());
            QCOMPARE(prefs->property("width").toInt(),2048);
            QCOMPARE(prefs->property("fps").toInt(),75);
            QCOMPARE(prefs->property("bitrateKbps").toInt(),125000);
            QCOMPARE(prefs->property("saves").toInt(),0);
            if(QString(name)=="SettingsHome") {
                QObject* choice=page->findChild<QObject*>("resolutionChoice"); QVERIFY(choice);
                QVERIFY(QMetaObject::invokeMethod(choice,"activated",Q_ARG(int,1)));
                QCOMPARE(prefs->property("width").toInt(),2560);
                QCOMPARE(prefs->property("height").toInt(),1440);
                QCOMPARE(prefs->property("saves").toInt(),1);
                QObject* sections=page->findChild<QObject*>("settingsSections"); QVERIFY(sections);
                for(int section=1; section<5; ++section) {
                    QVERIFY(sections->setProperty("currentIndex",section)); QTest::qWait(20);
                    QCOMPARE(prefs->property("saves").toInt(),1);
                    QVERIFY(page->property("contentHeight").toReal() > 0);
                }
                QObject* languages=page->findChild<QObject*>("languageChoice"); QVERIFY(languages);
                QCOMPARE(languages->property("currentIndex").toInt(),1);
                QVERIFY(QMetaObject::invokeMethod(languages,"activated",Q_ARG(int,1)));
                QCOMPARE(prefs->property("saves").toInt(),1);
                QVERIFY(QMetaObject::invokeMethod(languages,"activated",Q_ARG(int,2)));
                QCOMPARE(prefs->property("language").toInt(),3);
                QCOMPARE(prefs->property("saves").toInt(),2);
                QCOMPARE(prefs->property("retranslations").toInt(),1);
                QObject* cursor=page->findChild<QObject*>("localCursorSwitch"); QVERIFY(cursor);
                QVERIFY(cursor->setProperty("checked",false));
                QVERIFY(QMetaObject::invokeMethod(cursor,"clicked"));
                QVERIFY(!prefs->property("showLocalCursor").toBool());
                QCOMPARE(prefs->property("saves").toInt(),3);
                QObject* shared=page->findChild<QObject*>("sharedClipboardSwitch"); QVERIFY(shared);
                shared->setProperty("checked",true);
                QVERIFY(QMetaObject::invokeMethod(shared,"clicked"));
                QVERIFY(prefs->property("sharedClipboard").toBool());
                QObject* keys=page->findChild<QObject*>("systemKeysChoice"); QVERIFY(keys);
                QVERIFY(QMetaObject::invokeMethod(keys,"activated",Q_ARG(int,2)));
                QCOMPARE(prefs->property("captureSysKeysMode").toInt(),2);
                QCOMPARE(prefs->property("saves").toInt(),5);
            }
        }
        const auto bad=warnings.filter(QRegularExpression("ReferenceError|TypeError|binding loop|Binding loop|Cannot assign|Unable to assign"));
        QVERIFY2(bad.isEmpty(),qPrintable(bad.join('\n')));
    }
    void commonLanguagesRetranslateSettings() {
        QTemporaryDir directory;
        HostManager host(nullptr, directory.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("hostManager", &host);
        const QString gui=qEnvironmentVariable("TEST_GUI_DIR");
        QQmlComponent themeComponent(&engine,QUrl::fromLocalFile(gui+"/UiTheme.qml"));
        QScopedPointer<QObject> theme(themeComponent.create()); QVERIFY(theme);
        engine.rootContext()->setContextProperty("ui",theme.data());
        QQmlComponent component(&engine,QUrl::fromLocalFile(gui+"/SettingsHome.qml"));
        QScopedPointer<QObject> page(component.create()); QVERIFY2(page,qPrintable(component.errorString()));
        const QString english=page->property("heading").toString();
        QCOMPARE(english,QString("Make DeskPort your own."));
        auto windowMode=page->findChild<QObject*>("windowModeChoice"); QVERIFY(windowMode);
        auto systemKeys=page->findChild<QObject*>("systemKeysChoice"); QVERIFY(systemKeys);
        auto sections=page->findChild<QObject*>("settingsSections"); QVERIFY(sections);
        sections->setProperty("currentIndex",4);
        for (const auto& language : {"zh_CN","zh_TW","ja","ko","de","fr","es"}) {
            QTranslator translator;
            QVERIFY(translator.load(gui+"/../languages/qml_"+language+".qm"));
            QVERIFY(QCoreApplication::installTranslator(&translator));
            engine.retranslate();
            QVERIFY(page->property("heading").toString()!=english);
            QCOMPARE(windowMode->property("currentIndex").toInt(),2);
            QCOMPARE(systemKeys->property("currentIndex").toInt(),1);
            QCOMPARE(sections->property("currentIndex").toInt(),4);
            QVERIFY(!translator.translate("SettingsHome","Follow system").isEmpty());
            QVERIFY(!translator.translate("SettingsHome","Match the client window resolution").isEmpty());
            QVERIFY(!translator.translate("HostView","Built-in virtual display").isEmpty());
            QVERIFY(!translator.translate("HostView","Active · %1 × %2 pixels").isEmpty());
            QVERIFY(!translator.translate("BindingApproval","Allow & bind").isEmpty());
            QCoreApplication::removeTranslator(&translator);
            engine.retranslate();
            QCOMPARE(page->property("heading").toString(),english);
        }
    }
};
QTEST_MAIN(UiPages)
#include "ui-pages.moc"
