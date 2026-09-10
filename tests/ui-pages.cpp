#include <QtTest>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include "peermanager.h"

static QByteArray credential(const char* name) {
    QFile f(qEnvironmentVariable(name)); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
class TestSession : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    int executions = 0;
    Q_INVOKABLE void exec(QQuickWindow*) { ++executions; }
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
class UiPages : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qmlRegisterType<TestSession>("Session",1,0,"Session");
        qmlRegisterSingletonType<QObject>("SdlGamepadKeyNavigation",1,0,"SdlGamepadKeyNavigation",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine); c.setData("import QtQuick 2.9; QtObject { function enable() {} function disable() {} function getConnectedGamepads() { return 0 } }",QUrl()); return c.create();
        });
        qmlRegisterSingletonType<QObject>("ComputerManager",1,0,"ComputerManager",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine); c.setData("import QtQuick 2.9; QtObject { signal quitAppCompleted(var error) }",QUrl()); return c.create();
        });
        qmlRegisterSingletonType<QObject>("StreamingPreferences",1,0,"StreamingPreferences",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine);
            c.setData(R"(import QtQuick 2.9
QtObject {
 property int width: 2048; property int height: 1152; property int fps: 75; property int bitrateKbps: 125000
 property int windowMode: 2; property int captureSysKeysMode: 1; property int saves: 0
 property bool enableVsync: true; property bool absoluteMouseMode: true; property bool reverseScrollDirection: false
 property bool muteOnFocusLoss: true; property bool playAudioOnHost: false; property bool enableMdns: true; property bool keepAwake: true
 function save() { saves++ }
})",QUrl()); return c.create();
        });
        qmlRegisterSingletonType<QObject>("SystemProperties",1,0,"SystemProperties",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine); c.setData("import QtQuick 2.9; QtObject { property bool hasBrowser: false }",QUrl()); return c.create();
        });
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
        QVERIFY(QMetaObject::invokeMethod(root.data(),"back"));
        QVERIFY(root->property("navigationVisible").toBool());
        QVERIFY(QMetaObject::invokeMethod(root.data(),"startQuit"));
        QTRY_COMPARE(root->property("quits").toInt(),1);
        QVERIFY(!root->property("navigationVisible").toBool());
        QVERIFY(QMetaObject::invokeMethod(root.data(),"back"));
        QVERIFY(root->property("navigationVisible").toBool());
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
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
            }
        }
        const auto bad=warnings.filter(QRegularExpression("ReferenceError|TypeError|binding loop|Binding loop|Cannot assign|Unable to assign"));
        QVERIFY2(bad.isEmpty(),qPrintable(bad.join('\n')));
    }
};
QTEST_MAIN(UiPages)
#include "ui-pages.moc"
