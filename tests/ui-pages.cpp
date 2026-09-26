#include "diagnostics.h"
#include <QDesktopServices>
#include <cmath>
#include <QtTest>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTranslator>
#include <functional>
#include <QAbstractListModel>
#include <QDir>
#include "peermanager.h"
#include "hostlayout.h"
#include "manual.h"
#include "peerstore.h"
#include "singleinstance.h"
#include "../shared/deskport-core/portable/include/deskport/catalog.h"

static QQuickItem* findVisual(QQuickItem* item, const QString& name) {
    if (item->objectName() == name) return item;
    for (auto child : item->childItems()) if (auto found = findVisual(child, name)) return found;
    return nullptr;
}
static QByteArray credential(const char* name) {
    QFile f(qEnvironmentVariable(name)); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
class TestUpdates : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status MEMBER status CONSTANT)
    Q_PROPERTY(QString latestVersion MEMBER version CONSTANT)
    Q_PROPERTY(QString releaseNotes MEMBER notes CONSTANT)
    Q_PROPERTY(QString publishedAt MEMBER date CONSTANT)
    Q_PROPERTY(QString releaseUrl MEMBER url CONSTANT)
public:
    QString status = "available", version = "0.4.6", notes = "Release notes", date = "2026-09-21", url = "https://github.com/keithxc/deskport/releases/tag/v0.4.6";
    Q_INVOKABLE void start() {}
};
class TestSession : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_PROPERTY(QString hostId READ hostId CONSTANT)
    Q_PROPERTY(QString hostName READ hostName CONSTANT)
    QString hostId() const { return "device-a"; }
    QString hostName() const { return "Studio"; }
    Q_INVOKABLE QVariantMap traffic() const { return {{"received",862000000.0},{"sent",18000000.0}}; }
    Q_PROPERTY(bool viewerReady READ viewerReady NOTIFY viewerReadyChanged)
    bool nativeReady = false, viewerRequested = true;
    bool viewerReady() const { return nativeReady; }
    Q_INVOKABLE void setViewerRequested(bool visible) { viewerRequested = visible; }
    void makeViewerReady() { nativeReady = true; emit viewerReadyChanged(); }
    int delay = 0; bool cancelled = false;
    Q_INVOKABLE int retryDelay() const { return delay; }
    Q_INVOKABLE void cancelRecovery() { cancelled = true; }
    int executions = 0;
    QQuickWindow* receivedWindow = nullptr;
    TestSession* next = nullptr;
    std::function<void()> duringExec;
    Q_INVOKABLE bool adaptiveRestartPending() const { return next != nullptr; }
    Q_INVOKABLE TestSession* adaptiveContinuation() { auto value = next; next = nullptr; return value; }
    Q_INVOKABLE void exec(QQuickWindow* window) { receivedWindow = window; ++executions; if (duringExec) duringExec(); }
signals:
    void viewerReadyChanged();
    void stageStarting(QString stage);
    void stageFailed(QString stage, int error, QString ports);
    void connectionStarted();
    void displayLaunchError(QString text);
    void displayLaunchWarning(QString text);
    void quitStarting();
    void sessionFinished(int result);
    void readyForDeletion();
};
class TestComputers : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString currentGroup READ currentGroup WRITE setCurrentGroup NOTIFY currentGroupChanged)
public:
    using QAbstractListModel::QAbstractListModel;
    // Synthetic devices arranged by the real layout rules, stored in memory only.
    const QStringList devices{"device-a","device-b"};
    QVariantList stored; bool hasStored = false; QString group;
    // Screenshots show the trailing add card; behavior checks run without it.
    bool showAdd = false;
    bool hasAdd() const { return showAdd && group.isEmpty(); }
    Q_INVOKABLE void setShowAdd(bool value) { beginResetModel(); showAdd = value; endResetModel(); }
    HostLayout layout() { return HostLayout(stored, hasStored, {}, [this](const QVariantList& items){ stored = items; hasStored = true; }); }
    QVector<HostLayout::Entry> rows() const { return const_cast<TestComputers*>(this)->layout().entries(devices, group); }
    Q_INVOKABLE void initialize(QObject*) {}
    Q_INVOKABLE void refreshFavorites() {}
    QString currentGroup() const { return group; }
    void setCurrentGroup(const QString& value) { if (group == value) return; beginResetModel(); group = value; endResetModel(); emit currentGroupChanged(); }
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : rows().size() + (hasAdd() ? 1 : 0); }
    Q_INVOKABLE void moveComputer(int from,int to) {
        const int count = rows().size();
        if(from<0||to<0||from>=count||to>=count||from==to) return;
        beginMoveRows({},from,from,{},to>from?to+1:to); layout().move(from,to,devices,group); endMoveRows();
    }
    Q_INVOKABLE QString combine(int from,int to) {
        auto shown = rows();
        if(!group.isEmpty()||from<0||to<0||from>=shown.size()||to>=shown.size()||shown[from].group) return {};
        beginResetModel(); const QString id = layout().combine(shown[from].id, shown[to].id, devices, "Group"); endResetModel();
        return id;
    }
    Q_INVOKABLE QString addGroup(QString name) { beginResetModel(); const QString id = layout().addGroup(name, devices); endResetModel(); return id; }
    Q_INVOKABLE void renameGroup(QString id, QString name) { beginResetModel(); layout().rename(id, name); endResetModel(); }
    Q_INVOKABLE void deleteGroup(QString id) {
        beginResetModel(); layout().deleteGroup(id); if (group == id) group.clear(); endResetModel(); emit currentGroupChanged();
    }
    Q_INVOKABLE void moveOutOfGroup(int index) { auto shown = rows(); beginResetModel(); layout().moveOut(shown[index].id); endResetModel(); }
    Q_INVOKABLE QString groupName(QString id) { const QString name = layout().nameOf(id); return name.isEmpty() ? "Group" : name; }
    Q_INVOKABLE QString defaultGroupName() const { return "Group"; }
    Q_INVOKABLE bool canEdit() { return true; }
    QHash<int,QByteArray> roleNames() const override {
        return {{Qt::UserRole,"name"},{Qt::UserRole+1,"hostId"},{Qt::UserRole+2,"online"},
        {Qt::UserRole+3,"paired"},{Qt::UserRole+4,"statusUnknown"},{Qt::UserRole+5,"address"},
        {Qt::UserRole+6,"favorite"},{Qt::UserRole+7,"sourceIndex"},{Qt::UserRole+8,"details"},
        {Qt::UserRole+11,"operatingSystem"},{Qt::UserRole+9,"serverSupported"},{Qt::UserRole+10,"wakeable"},
        {Qt::UserRole+12,"isGroup"},{Qt::UserRole+13,"groupId"},{Qt::UserRole+14,"memberCount"},{Qt::UserRole+15,"memberSystems"},{Qt::UserRole+16,"isAdd"}};
    }
    QVariant data(const QModelIndex& index,int role) const override {
        const auto shown = rows();
        if (hasAdd() && index.row() == shown.size()) {
            switch(role-Qt::UserRole) {
            case 16: return true;
            case 2: case 3: case 4: case 9: case 10: case 6: case 12: return false;
            case 7: return -1;
            case 14: return 0;
            case 15: return QStringList();
            default: return QString();
            }
        }
        if (index.row() < 0 || index.row() >= shown.size()) return {};
        const auto entry = shown[index.row()];
        if (entry.group) {
            switch(role-Qt::UserRole) {
            case 0: return entry.name.isEmpty() ? QString("Group") : entry.name;
            case 1: case 5: case 8: case 11: return QString();
            case 2: case 3: case 4: case 9: case 10: case 6: return false;
            case 7: return -1;
            case 12: return true;
            case 13: return entry.id;
            case 14: return int(entry.devices.size());
            case 16: return false;
            case 15: { QStringList systems; for (const auto& id : entry.devices) systems << (id == "device-a" ? "macOS" : "NixOS"); return systems; }
            default: return {};
            }
        }
        const bool first = entry.id == "device-a";
        switch(role-Qt::UserRole) {
        case 0: return first ? "Studio" : "Travel laptop";
        case 1: return first ? "device-a" : "device-b";
        case 2: case 3: case 9: return true;
        case 4: case 10: return false;
        case 5: return "example.invalid";
        case 6: return first;
        case 7: return first ? 1 : 0;
        case 8: return "Synthetic device details";
        case 11: return first ? "macOS" : "NixOS";
        case 12: return false;
        case 13: return QString();
        case 14: return 0;
        case 15: return QStringList();
        case 16: return false;
        default: return {};
        }
    }
signals:
    void currentGroupChanged();
    void pairingCompleted(QVariant error);
    void connectionTestCompleted(int result,QString ports);
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
    Q_PROPERTY(QVariantList desktopAdjustmentChoices READ adjustmentChoices CONSTANT)
    Q_PROPERTY(QStringList desktopAdjustmentLabels READ adjustmentLabels CONSTANT)
    QVariantList adjustmentChoices() const {
        QVariantList values;
        for (double value : dp_catalog_tuning_values) values.append(value);
        return values;
    }
    QStringList adjustmentLabels() const {
        QStringList labels;
        for (const char *label : dp_catalog_tuning_labels) labels.append(QString::fromLatin1(label));
        return labels;
    }
    enum Language { LANG_AUTO, LANG_EN, LANG_FR, LANG_ZH_CN, LANG_DE, LANG_NB_NO, LANG_RU, LANG_ES, LANG_JA, LANG_VI, LANG_TH, LANG_KO, LANG_HU, LANG_NL, LANG_SV, LANG_TR, LANG_UK, LANG_ZH_TW, LANG_PT, LANG_PT_BR, LANG_EL, LANG_IT, LANG_HI, LANG_PL, LANG_CS, LANG_HE, LANG_CKB, LANG_LT, LANG_ET };
    Q_ENUM(Language)
};
class PreviewHost : public HostManager {
    Q_OBJECT
    Q_PROPERTY(QString deviceName READ previewName CONSTANT)
    Q_PROPERTY(QVariantList permissions READ previewPermissions NOTIFY permissionsChanged)
    Q_PROPERTY(QString readiness READ previewReadiness NOTIFY permissionsChanged)
public:
    using HostManager::HostManager;
    QString previewName() const { return "This computer"; }
    QString previewReadiness() const { return "attention"; }
    QVariantList previewPermissions() const {
        return {QVariantMap{{"key","screen"},{"title","Screen recording"},{"purpose","Synthetic screen permission"},{"state","allowed"}},
                QVariantMap{{"key","input"},{"title","Keyboard and pointer"},{"purpose","Synthetic input permission"},{"state","needsSetup"}}};
    }
};
class UiPages : public QObject {
    Q_OBJECT
    QUrl feedbackUrl;
public slots:
    void captureFeedback(const QUrl& url) { feedbackUrl=url; }
private slots:
    void diagnosticsControls() {
        QTemporaryDir dir;
        Diagnostics logs(nullptr,dir.path()+"/diagnostics");
        QQmlEngine engine;
        const QString gui=qEnvironmentVariable("TEST_GUI_DIR");
        QQmlComponent themeComponent(&engine,QUrl::fromLocalFile(gui+"/UiTheme.qml"));
        QScopedPointer<QObject> theme(themeComponent.create()); QVERIFY(theme);
        engine.rootContext()->setContextProperty("ui",theme.data());
        engine.rootContext()->setContextProperty("diagnostics",&logs);
        QQmlComponent component(&engine,QUrl::fromLocalFile(gui+"/SettingsHome.qml"));
        QScopedPointer<QObject> page(component.create()); QVERIFY2(page,qPrintable(component.errorString()));
        auto item=qobject_cast<QQuickItem*>(page.data()); QVERIFY(item);
        QQuickWindow window; window.resize(660,900);
        item->setParentItem(window.contentItem()); item->setSize(QSizeF(660,900));
        window.show(); QTest::qWait(100);
        auto toggle=page->findChild<QQuickItem*>("diagnosticsSwitch"); QVERIFY(toggle);
        QVERIFY(!toggle->property("checked").toBool());
        QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,toggle->mapToScene(QPointF(toggle->width()/2,toggle->height()/2)).toPoint());
        QTRY_VERIFY(logs.enabled());
        auto button=page->findChild<QObject*>("feedbackButton"); QVERIFY(button);
        auto notice=page->findChild<QObject*>("diagnosticsPublicNotice"); QVERIFY(notice);
        QVERIFY(notice->property("text").toString().contains("public"));
        QDesktopServices::setUrlHandler("https",this,"captureFeedback");
        QVERIFY(QMetaObject::invokeMethod(button,"clicked"));
        QVERIFY(QFile::exists(logs.bundlePath()));
        QCOMPARE(feedbackUrl,Diagnostics::issueUrl());
        QVERIFY(!feedbackUrl.toString().contains(dir.path()));
        QDesktopServices::unsetUrlHandler("https");
        QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,toggle->mapToScene(QPointF(toggle->width()/2,toggle->height()/2)).toPoint());
        QTRY_VERIFY(!logs.enabled());
        const QString shots=qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS");
        if (!shots.isEmpty()) { QDir().mkpath(shots); QTest::qWait(100); QVERIFY(window.grabWindow().save(shots+"/diagnostics.png")); }
    }

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
        qmlRegisterType<TestComputers>("ComputerModel",1,0,"ComputerModel");
        qmlRegisterSingletonType<QObject>("AutoUpdateChecker",1,0,"AutoUpdateChecker",+[](QQmlEngine*,QJSEngine*) -> QObject* { return new TestUpdates; });
        qmlRegisterType<TestSession>("Session",1,0,"Session");
        qmlRegisterSingletonType<Manual>("Manual",1,0,"Manual",+[](QQmlEngine*,QJSEngine*) -> QObject* { return new Manual; });
        qmlRegisterType<TestDesktopApps>("AppModel",1,0,"AppModel");
        qmlRegisterSingletonType<QObject>("SdlGamepadKeyNavigation",1,0,"SdlGamepadKeyNavigation",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine); c.setData("import QtQuick 2.9; QtObject { property int enables: 0; function enable() { enables++ } function disable() {} function getConnectedGamepads() { return 0 } }",QUrl()); return c.create();
        });
        qmlRegisterSingletonType<QObject>("ComputerManager",1,0,"ComputerManager",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine); c.setData("import QtQuick 2.9; QtObject { signal quitAppCompleted(var error); signal computerAddCompleted(bool success, bool blocked); function startPolling() {} function stopPollingAsync() {} function addBoundHost(peer) {} }",QUrl()); return c.create();
        });
        qmlRegisterType<TestPreferences>("TestPreferences",1,0,"TestPreferences");
        qmlRegisterSingletonType<TestPreferences>("StreamingPreferences",1,0,"StreamingPreferences",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine);
            c.setData(R"(import QtQuick 2.9
import TestPreferences 1.0
TestPreferences {
 property int displayPolicy: 0
 property double desktopAdjustment: 1.0
 property string deviceId: ""; property bool remoteAudio: true; property bool remoteInput: true
 property int uiAccent: 1; property bool showTraffic: true; property int uiTheme: 0; property bool compactDevices: true; property int uiDisplayMode: 0
 property int language: 1; property int retranslations: 0
 function retranslate() { retranslations++; return true }
 function manualLanguage() { return "en" }
 property int width: 2048; property int height: 1152; property int fps: 75; property int bitrateKbps: 125000
 property int windowMode: 2; property int recommendedFullScreenMode: 1; property int captureSysKeysMode: 1; property int saves: 0
 property bool smartStreaming: true; property bool framePacing: false; property bool showPerformanceOverlay: false
 property bool sharedClipboard: false; property bool showLocalCursor: true; property bool adaptiveResolution: true; property bool enableVsync: true; property bool absoluteMouseMode: true; property bool reverseScrollDirection: false
 property bool muteOnFocusLoss: true; property bool playAudioOnHost: false; property bool enableMdns: true; property bool keepAwake: true
 function forDevice(id) { deviceId = id; return this }
 function save() { saves++ }
})",QUrl()); return c.create();
        });
        qmlRegisterSingletonType<QObject>("SystemProperties",1,0,"SystemProperties",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine); c.setData("import QtQuick 2.9; QtObject { property bool systemDark: false; property color systemAccent: \"#3269d7\"; property bool hasBrowser: false; property bool hasDesktopEnvironment: true; property bool isWow64: false; property bool hasHardwareAcceleration: true; property bool isRunningXWayland: false; property string unmappedGamepads: \"\"; property string friendlyNativeArchName: \"test\"; property string versionString: \"test\"; property int memorySamples: 0; function memoryUsage(pid) { memorySamples++; return {available: true, complete: true, client: 10485760, host: 0, helpers: 0, total: 10485760} } }",QUrl()); return c.create();
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
import SdlGamepadKeyNavigation 1.0
ApplicationWindow {
 id: window; width: 800; height: 600
 property int navigationEnables: SdlGamepadKeyNavigation.enables
 StackView { id: stackView; anchors.fill: parent; initialItem: Item {} }
 function start() { stackView.push(Qt.resolvedUrl("StreamSegue.qml"), {session: testSession, appName: "Test"}, StackView.Immediate) }
})",QUrl::fromLocalFile(qEnvironmentVariable("TEST_GUI_DIR")+"/nested-harness.qml"));
        QScopedPointer<QObject> root(harness.create()); QVERIFY2(root,qPrintable(harness.errorString()));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"start"));
        QTRY_COMPARE(next.executions,1);
        QCOMPARE(root->property("navigationEnables").toInt(), 0);
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
import SdlGamepadKeyNavigation 1.0
ApplicationWindow {
 id: window; width: 800; height: 600
 property int navigationEnables: SdlGamepadKeyNavigation.enables
 StackView { id: stackView; anchors.fill: parent; initialItem: Item {} }
 function start() { stackView.push(Qt.resolvedUrl("StreamSegue.qml"), {session: testSession, appName: "Test"}, StackView.Immediate) }
})",QUrl::fromLocalFile(qEnvironmentVariable("TEST_GUI_DIR")+"/deferred-harness.qml"));
        QScopedPointer<QObject> root(harness.create()); QVERIFY2(root,qPrintable(harness.errorString()));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"start"));
        QTRY_COMPARE(next.executions,1);
        QCOMPARE(root->property("navigationEnables").toInt(), 0);
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
    void reconnectDelayCanBeCancelled() {
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        TestSession session; session.delay = 10000;
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
        QTest::qWait(100);
        QCOMPARE(session.executions,0);
        auto cancel=root->findChild<QObject*>("cancelReconnect"); QVERIFY(cancel);
        QVERIFY(QMetaObject::invokeMethod(cancel,"clicked"));
        QVERIFY(session.cancelled);
        QTRY_COMPARE(session.executions,1); // Normal cleanup owner runs exactly once.
        emit session.sessionFinished(0);
        emit session.readyForDeletion();
        QTest::qWait(150);
        QCOMPARE(session.executions,1);
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
        root->setProperty("visible", true);
        emit session.connectionStarted();
        QVERIFY(root->property("visible").toBool());
        QCOMPARE(session.executions, 1);
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
        for (const auto& value : {"ready", "waiting", "background", "missing", "busy", "cancel"}) QTest::newRow(value) << QString(value);
    }
    void desktopShortcut() {
        QFETCH(QString, state);
        desktopTestState = (state == "cancel" || state == "background") ? "waiting" : state; desktopCreateCalls = 0;
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
 function showDevices() { stackView.push(controlPage, StackView.Immediate) }
 Component { id: controlPage; Item { property bool controlCenterForActiveSession: true } }
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
        if (state == "background") QVERIFY(QMetaObject::invokeMethod(root.data(), "showDevices"));
        if (state == "waiting" || state == "background") {
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
        QCOMPARE(root->property("depth").toInt(), state == "background" ? 3 : 2);
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
        engine.rootContext()->setContextProperty("diagnostics", &Diagnostics::instance());
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
                QVERIFY(!page->findChild<QObject*>("resolutionChoice"));
                QVERIFY(!page->findChild<QObject*>("settingsSections"));
                auto themeChoice=page->findChild<QObject*>("themeChoice"); QVERIFY(themeChoice);
                QVERIFY(QMetaObject::invokeMethod(themeChoice,"activated",Q_ARG(int,2)));
                QCOMPARE(prefs->property("uiTheme").toInt(),2);
                auto accent=page->findChild<QObject*>("accentChoice"); QVERIFY(accent);
                QVERIFY(QMetaObject::invokeMethod(accent,"activated",Q_ARG(int,3)));
                QCOMPARE(prefs->property("uiAccent").toInt(),3);
                auto traffic=page->findChild<QObject*>("showTrafficSwitch"); QVERIFY(traffic);
                traffic->setProperty("checked",false);
                QVERIFY(QMetaObject::invokeMethod(traffic,"clicked"));
                QVERIFY(!prefs->property("showTraffic").toBool());
                QCOMPARE(prefs->property("bitrateKbps").toInt(),125000);
                QObject* languages=page->findChild<QObject*>("languageChoice"); QVERIFY(languages);
                QVERIFY(QMetaObject::invokeMethod(languages,"activated",Q_ARG(int,2)));
                QCOMPARE(prefs->property("language").toInt(),3);
                QCOMPARE(prefs->property("retranslations").toInt(),1);

            }
        }
        const auto bad=warnings.filter(QRegularExpression("ReferenceError|TypeError|binding loop|Binding loop|Cannot assign|Unable to assign|Missing parent|Component is not ready"));
        QVERIFY2(bad.isEmpty(),qPrintable(bad.join('\n')));
    }
    void clientOnlyBindingPageDoesNotAdvertiseSharing() {
        QTemporaryDir dir;
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager peers(&host,credential("TEST_CERT_A"),credential("TEST_KEY_A"),
            dir.path()+"/peers",0,QHostAddress::LocalHost,PeerManager::Mode::ClientOnly);
        QQmlEngine engine;
        const QString gui=qEnvironmentVariable("TEST_GUI_DIR");
        QQmlComponent themeComponent(&engine,QUrl::fromLocalFile(gui+"/UiTheme.qml"));
        QScopedPointer<QObject> theme(themeComponent.create()); QVERIFY(theme);
        engine.rootContext()->setContextProperty("ui",theme.data());
        engine.rootContext()->setContextProperty("hostManager",&host);
        engine.rootContext()->setContextProperty("peerManager",&peers);
        QQmlComponent component(&engine,QUrl::fromLocalFile(gui+"/BindView.qml"));
        QVERIFY2(component.isReady(),qPrintable(component.errorString()));
        QScopedPointer<QObject> page(component.create()); QVERIFY2(page,qPrintable(component.errorString()));
        QCOMPARE(page->property("heading").toString(),QString("Request access to a computer"));
        auto text=page->findChild<QObject*>("bindingScope"); QVERIFY(text);
        QVERIFY(text->property("text").toString().contains("does not share its own desktop"));
        QCOMPARE(peers.port(),0); QVERIFY(!host.running()); QVERIFY(peers.peers().isEmpty());
    }
    void themeFollowsSystemAndAllowsIndependentOverrides() {
        QQmlEngine engine;
        QQmlComponent component(&engine,QUrl::fromLocalFile(qEnvironmentVariable("TEST_GUI_DIR")+"/UiTheme.qml"));
        QScopedPointer<QObject> theme(component.create()); QVERIFY(theme);
        theme->setProperty("systemDark",true); QVERIFY(theme->property("dark").toBool());
        theme->setProperty("mode",1); QVERIFY(!theme->property("dark").toBool());
        theme->setProperty("mode",2); theme->setProperty("systemDark",false); QVERIFY(theme->property("dark").toBool());
        theme->setProperty("mode",0); QVERIFY(!theme->property("dark").toBool());
        theme->setProperty("systemAccent",QColor("#8055bf"));
        QCOMPARE(theme->property("baseAccent").value<QColor>(),QColor("#8055bf"));
        theme->setProperty("accentMode",1);
        auto fixed=theme->property("baseAccent");
        theme->setProperty("systemAccent",QColor("#23754f")); QCOMPARE(theme->property("baseAccent"),fixed);
        for(int mode=1;mode<=2;++mode) for(int accent=0;accent<=4;++accent) {
            theme->setProperty("mode",mode); theme->setProperty("accentMode",accent);
            auto color=theme->property("accent").value<QColor>();
            auto text=theme->property("accentText").value<QColor>();
            auto luminance=[](QColor c) {
                auto f=[](double v){ return v<=0.04045 ? v/12.92 : std::pow((v+0.055)/1.055,2.4); };
                return 0.2126*f(c.redF())+0.7152*f(c.greenF())+0.0722*f(c.blueF());
            };
            double a=luminance(color),b=luminance(text);
            QVERIFY((qMax(a,b)+0.05)/(qMin(a,b)+0.05)>=4.5);
        }
    }
    void deviceSettingsDoNotDuplicateAddressEditing() {
        QQmlEngine engine;
        const auto gui = qEnvironmentVariable("TEST_GUI_DIR");
        QQmlComponent themeComponent(&engine, QUrl::fromLocalFile(gui + "/UiTheme.qml"));
        QScopedPointer<QObject> theme(themeComponent.create()); QVERIFY(theme);
        engine.rootContext()->setContextProperty("ui", theme.data());
        QQmlComponent component(&engine);
        component.setData("import QtQuick 2.9; import StreamingPreferences 1.0; DeviceSettings { preferences: StreamingPreferences; deviceName: \"Studio\"; deviceId: \"device-a\" }", QUrl::fromLocalFile(gui + "/address-test.qml"));
        QScopedPointer<QObject> page(component.create()); QVERIFY2(page, qPrintable(component.errorString()));
        QVERIFY(!page->findChild<QObject*>("changeDeviceAddress"));
        QVERIFY(!page->findChild<QObject*>("savedDeviceAddress"));
        QVERIFY(!page->findChild<QObject*>("editPeerAddress"));
    }
    void deviceSettingsAreSeparate() {
        QTemporaryDir directory;
        PreviewHost host(nullptr,directory.path()+"/host");
        PeerManager manager(&host,credential("TEST_CERT_A"),credential("TEST_KEY_A"),directory.path()+"/binding",0,QHostAddress::LocalHost);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("peerManager",&manager);
        const QString gui=qEnvironmentVariable("TEST_GUI_DIR");
        QQmlComponent themeComponent(&engine,QUrl::fromLocalFile(gui+"/UiTheme.qml"));
        QScopedPointer<QObject> theme(themeComponent.create()); QVERIFY(theme);
        engine.rootContext()->setContextProperty("ui",theme.data());
        QQmlComponent accessComponent(&engine);
        accessComponent.setData("import QtQuick 2.9; import StreamingPreferences 1.0; QtObject { property var prefs: StreamingPreferences }",QUrl());
        QScopedPointer<QObject> access(accessComponent.create());
        auto prefs=access->property("prefs").value<QObject*>(); QVERIFY(prefs);
        prefs->setProperty("deviceId","synthetic-device");
        engine.rootContext()->setContextProperty("testPrefs",prefs);
        QQmlComponent component(&engine);
        component.setData("import QtQuick 2.9; DeviceSettings { preferences: testPrefs; deviceName: \"Studio\" }",QUrl::fromLocalFile(gui+"/device-test.qml"));
        QScopedPointer<QObject> page(component.create()); QVERIFY2(page,qPrintable(component.errorString()));
        QVERIFY(!page->findChild<QObject*>("themeChoice"));
        auto mode=page->findChild<QObject*>("devicePictureMode"); QVERIFY(mode);
        QCOMPARE(prefs->property("saves").toInt(),0);
        QVERIFY(QMetaObject::invokeMethod(mode,"activated",Q_ARG(int,3)));
        QCOMPARE(prefs->property("fps").toInt(),30);
        QCOMPARE(prefs->property("bitrateKbps").toInt(),5000);
        QVERIFY(!prefs->property("smartStreaming").toBool());
        QVERIFY(QMetaObject::invokeMethod(mode,"activated",Q_ARG(int,0)));
        QVERIFY(prefs->property("smartStreaming").toBool());
        QCOMPARE(prefs->property("uiTheme").toInt(),0);
        auto policy=page->findChild<QObject*>("deviceDisplayPolicy"); QVERIFY(policy);
        QCOMPARE(policy->property("currentIndex").toInt(),0);
        policy->setProperty("currentIndex",2);
        QVERIFY(QMetaObject::invokeMethod(policy,"activated",Q_ARG(int,2)));
        QCOMPARE(prefs->property("displayPolicy").toInt(),2);
        auto adjustment=page->findChild<QObject*>("deviceDesktopAdjustment"); QVERIFY(adjustment);
        QCOMPARE(adjustment->property("currentIndex").toInt(),5);
        QVERIFY(QMetaObject::invokeMethod(adjustment,"activated",Q_ARG(int,0)));
        QCOMPARE(prefs->property("desktopAdjustment").toDouble(),0.5);
        QVERIFY(QMetaObject::invokeMethod(adjustment,"activated",Q_ARG(int,9)));
        QCOMPARE(prefs->property("desktopAdjustment").toDouble(),1.5);
        auto full=page->findChild<QObject*>("deviceFullScreen"); QVERIFY(full);
        QVERIFY(!full->property("checked").toBool());
        full->setProperty("checked",true); QVERIFY(QMetaObject::invokeMethod(full,"clicked"));
        QCOMPARE(prefs->property("windowMode").toInt(),1);
        full->setProperty("checked",false); QVERIFY(QMetaObject::invokeMethod(full,"clicked"));
        QCOMPARE(prefs->property("windowMode").toInt(),2);
        QVERIFY(page->findChild<QObject*>("deviceAdvancedButton"));
    }
    void navigationAndDeviceIdentity_data() {
        QTest::addColumn<QString>("outcome");
        for (auto name : {"streaming", "failure", "cancel"}) QTest::newRow(name) << QString(name);
    }
    void navigationAndDeviceIdentity() {
        QFETCH(QString, outcome);
        QTemporaryDir directory;
        PreviewHost host(nullptr,directory.path()+"/host");
        PeerManager peers(&host,credential("TEST_CERT_A"),credential("TEST_KEY_A"),directory.path()+"/peers",0,QHostAddress::LocalHost);
        TestSession session;
        QQmlEngine::setObjectOwnership(&session,QQmlEngine::CppOwnership);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("diagnostics", &Diagnostics::instance());
        engine.rootContext()->setContextProperty("hostManager", &host);
        engine.rootContext()->setContextProperty("peerManager", &peers);
        engine.rootContext()->setContextProperty("initialView", QString("qrc:/gui/PcView.qml"));
        engine.rootContext()->setContextProperty("startInBackground", true);
        engine.rootContext()->setContextProperty("startSharingPage", false);
        engine.rootContext()->setContextProperty("testSession", &session);
        QFile source(qEnvironmentVariable("TEST_GUI_DIR")+"/main.qml"); QVERIFY(source.open(QIODevice::ReadOnly));
        auto qml=source.readAll();
        qml.insert(qml.lastIndexOf('}'), R"(
 function testStart() { stackView.push("qrc:/gui/StreamSegue.qml", {session:testSession, appName:"Desktop"}, StackView.Immediate) }
 function testSettings() { showDevices(); navigateTo("qrc:/gui/SettingsHome.qml", "SettingsHome") }
 function testSharing() { showDevices(); navigateTo("qrc:/gui/HostView.qml", "HostView") }
 function testBinding() { showDevices(); navigateTo("qrc:/gui/BindView.qml", "BindView") }
 function testGrid() { return stackView.currentItem }
 function testDevice() { stackView.push("qrc:/gui/DeviceSettings.qml", {preferences: StreamingPreferences.forDevice("device-a"), deviceName: "Studio"}, StackView.Immediate) }
 function testCards() { StreamingPreferences.compactDevices = false }
 function testSameSettings() { navigateTo("qrc:/gui/SettingsHome.qml", "SettingsHome") }
 property alias testDepth: stackView.depth
 property alias testCurrentPage: stackView.currentItem
)");
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        QQmlComponent component(&engine); component.setData(qml,QUrl("qrc:/gui/main.qml"));
        QScopedPointer<QObject> root(component.create()); QVERIFY2(root,qPrintable(component.errorString()));
        auto window=qobject_cast<QQuickWindow*>(root.data()); QVERIFY(window);
        window->resize(1120,620); window->show(); QTest::qWait(100);
        auto memoryButton = findVisual(window->contentItem(), "memorySummary"); QVERIFY(memoryButton);
        QVERIFY(memoryButton->property("text").toString().contains("10 MiB"));
        QVERIFY(QMetaObject::invokeMethod(memoryButton, "clicked"));
        auto memoryPopup = root->findChild<QObject*>("memoryDetails"); QVERIFY(memoryPopup);
        QVERIFY(memoryPopup->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(memoryPopup, "close"));
        window->resize(800,620); QTest::qWait(100);
        auto updateButton = findVisual(window->contentItem(), "versionUpdateButton"); QVERIFY(updateButton);
        QVERIFY(updateButton->property("highlighted").toBool());
        QVERIFY(QMetaObject::invokeMethod(updateButton, "clicked"));
        auto updatePopup = root->findChild<QObject*>("updateDialog"); QVERIFY(updatePopup);
        QVERIFY(updatePopup->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(updatePopup, "close"));
        QCOMPARE(root->property("testDepth").toInt(),2);
        // Discard only initial setup navigation, which is scheduled once.
        QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevices"));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"testBinding")); QTest::qWait(100);
        QCOMPARE(root->property("testCurrentPage").value<QObject*>()->objectName(),QString("Add a device"));
        const QVariantMap newlyBound{{"name","New computer"}};
        QVERIFY(QMetaObject::invokeMethod(&peers,"peerBound",Q_ARG(QVariantMap,newlyBound)));
        QTRY_COMPARE(root->property("testDepth").toInt(),1);
        QCOMPARE(root->property("testCurrentPage").value<QObject*>()->objectName(),QString("Devices"));
        QCOMPARE(session.executions, 0); // Binding never starts a stream.
        QVERIFY(QMetaObject::invokeMethod(root.data(),"testStart"));
        QTRY_COMPARE(session.executions,1);
        QCOMPARE(root->property("activeHostId").toString(),QString("device-a"));
        QVERIFY(QMetaObject::invokeMethod(&peers,"peerBound",Q_ARG(QVariantMap,newlyBound)));
        QTest::qWait(100);
        QCOMPARE(session.executions, 1); // An incoming binding cannot replace it.
        QCOMPARE(root->property("activeHostId").toString(),QString("device-a"));
        // Hold transport/window creation pending: returning must keep progress
        // visible even after the connection callback (which is not window-ready).
        QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevicesDuringSession"));
        QVERIFY(!session.viewerRequested);
        QVERIFY(QMetaObject::invokeMethod(root.data(),"prepareViewerRecall"));
        QVERIFY(window->isVisible());
        QVERIFY(session.viewerRequested);
        emit session.connectionStarted();
        QVERIFY(window->isVisible());
        QTest::qWait(250);
        QVERIFY(window->isVisible());
        QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevicesDuringSession"));
        if (outcome != "streaming") {
            if (outcome == "cancel") {
                QVERIFY(QMetaObject::invokeMethod(root.data(),"prepareViewerRecall"));
                auto cancel = root->findChild<QObject*>("cancelReconnect"); QVERIFY(cancel);
                QVERIFY(QMetaObject::invokeMethod(cancel,"clicked"));
                QVERIFY(session.cancelled);
            } else emit session.stageFailed("Synthetic delayed host", -1, "");
            emit session.sessionFinished(0);
            QTRY_COMPARE(root->property("testDepth").toInt(),1);
            QVERIFY(window->isVisible());
            emit session.readyForDeletion();
            QTest::qWait(50);
            session.makeViewerReady(); // Late callback cannot hide Devices.
            QVERIFY(window->isVisible());
            QCOMPARE(session.executions,1);
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
            return;
        }
        session.makeViewerReady();
        QVERIFY(window->isVisible()); // Late readiness must not steal Devices.
        QVERIFY(QMetaObject::invokeMethod(root.data(),"prepareViewerRecall"));
        QVERIFY(!window->isVisible());
        int recalls=0;
        connect(&host,&HostManager::viewerRecallRequested,this,[&]{recalls++;});
        for(int i=0;i<50;++i) {
            QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevicesDuringSession"));
            QCOMPARE(root->property("testDepth").toInt(),3);
            QVERIFY(QMetaObject::invokeMethod(root.data(),"testSettings"));
            QCOMPARE(root->property("testDepth").toInt(),4);
            QVERIFY(QMetaObject::invokeMethod(root.data(),"testSameSettings"));
            QCOMPARE(root->property("testDepth").toInt(),4);
            QVERIFY(QMetaObject::invokeMethod(root.data(),"testSharing"));
            QCOMPARE(root->property("testDepth").toInt(),4);
            QVERIFY(QMetaObject::invokeMethod(root.data(),"prepareViewerRecall"));
            QCOMPARE(root->property("testDepth").toInt(),2);
            QVERIFY(!window->isVisible());
            QTest::qWait(20);
        }
        QCOMPARE(session.executions,1);
        QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevicesDuringSession"));
        QTest::qWait(150);
        auto grid=root->property("testCurrentPage").value<QObject*>(); QVERIFY(grid);
        QCOMPARE(grid->property("count").toInt(),2);
        auto gridItem=qobject_cast<QQuickItem*>(grid); QVERIFY(gridItem);
        auto a=findVisual(gridItem,"device-device-a");
        auto b=findVisual(gridItem,"device-device-b");
        QVERIFY(a); QVERIFY(b);
        auto cardB=b->property("contentItem").value<QObject*>(); QVERIFY(cardB);
        auto cardA=a->property("contentItem").value<QObject*>(); QVERIFY(cardA);
        const int devicePageDepth = root->property("testDepth").toInt();
        QVERIFY(QMetaObject::invokeMethod(cardA,"settingsRequested"));
        auto deviceSettingsDialog = grid->findChild<QObject*>("deviceSettingsDialog"); QVERIFY(deviceSettingsDialog);
        QTRY_VERIFY(deviceSettingsDialog->property("visible").toBool());
        QCOMPARE(root->property("testDepth").toInt(),devicePageDepth);
        QVERIFY(!deviceSettingsDialog->findChild<QObject*>("changeDeviceAddress"));
        if(!qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS").isEmpty()) {
            QTest::qWait(200);
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS")+"/device-settings-popup.png"));
        }
        auto closeDeviceSettings = deviceSettingsDialog->findChild<QObject*>("closeDeviceSettings"); QVERIFY(closeDeviceSettings);
        QVERIFY(QMetaObject::invokeMethod(closeDeviceSettings,"clicked"));
        QTRY_VERIFY(!deviceSettingsDialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(cardB,"activateRequested"));
        QCOMPARE(recalls,0);
        QVERIFY(QMetaObject::invokeMethod(cardA,"activateRequested"));
        QCOMPARE(recalls,1);
        auto header=findVisual(gridItem,"deviceHeader"); QVERIFY(header);
        auto first=qobject_cast<QQuickItem*>(a); QVERIFY(first);
        QVERIFY(first->mapToScene(QPointF()).y() >= header->mapToScene(QPointF(0,header->height())).y());
        // Arrange mode: dragging the second card over the first swaps them in the model.
        auto openDetails=grid->findChild<QObject*>("deviceDetails"); QVERIFY(openDetails);
        QVERIFY(QMetaObject::invokeMethod(openDetails,"close")); QTest::qWait(300);
        // Manual, then device controls, sharing and settings sit in the top bar.
        auto arrange=findVisual(window->contentItem(),"arrangeDevices"); QVERIFY(arrange);
        auto manual=findVisual(window->contentItem(),"manualButton"); QVERIFY(manual);
        auto devices=findVisual(window->contentItem(),"devicesButton"); QVERIFY(devices);
        auto refresh=findVisual(window->contentItem(),"refreshDevices"); QVERIFY(refresh);
        auto sharing=findVisual(window->contentItem(),"sharingButton"); QVERIFY(sharing);
        auto settings=findVisual(window->contentItem(),"settingsButton"); QVERIFY(settings);
        QVERIFY(manual->mapToScene(QPointF()).x() < devices->mapToScene(QPointF()).x());
        QVERIFY(devices->mapToScene(QPointF()).x() < arrange->mapToScene(QPointF()).x());
        QVERIFY(arrange->mapToScene(QPointF()).x() < refresh->mapToScene(QPointF()).x());
        QVERIFY(refresh->mapToScene(QPointF()).x() < sharing->mapToScene(QPointF()).x());
        QVERIFY(sharing->mapToScene(QPointF()).x() < settings->mapToScene(QPointF()).x());
        QVERIFY(QMetaObject::invokeMethod(refresh,"clicked"));
        QTest::qWait(80);
        QVERIFY(std::abs(refresh->property("rotation").toReal()) > 1.0);
        QVERIFY(findVisual(window->contentItem(),"settingsButton"));
        QVERIFY(findVisual(window->contentItem(),"sharingButton")); QVERIFY(findVisual(window->contentItem(),"trafficSummary"));
        // The right-hand buttons stay inside the window at every width.
        for (int width : {1120, 800, 640}) {
            const qreal previousSettledWidth=grid->property("settledWidth").toReal();
            window->resize(width, 620); QTest::qWait(10);
            QVERIFY(grid->property("resizing").toBool());
            QCOMPARE(grid->property("settledWidth").toReal(),previousSettledWidth);
            QTRY_VERIFY_WITH_TIMEOUT(!grid->property("resizing").toBool(),250);
            QCOMPARE(grid->property("settledWidth").toReal(),grid->property("width").toReal());
            auto settingsButton=findVisual(window->contentItem(),"settingsButton");
            QVERIFY(settingsButton->mapToScene(QPointF(settingsButton->width(),0)).x() <= width);
        }
        window->resize(800,620); QTest::qWait(100);
        QVERIFY(arrange->isVisible());
        QVERIFY(QMetaObject::invokeMethod(arrange,"clicked"));
        QVERIFY(grid->property("arranging").toBool());
        QVERIFY(arrange->property("editing").toBool());
        auto computers=qobject_cast<QAbstractListModel*>(grid->property("model").value<QObject*>()); QVERIFY(computers);
        QTest::qWait(100);
        auto second=qobject_cast<QQuickItem*>(b); QVERIFY(second);
        const QPoint from=second->mapToScene(QPointF(second->width()/2,second->height()/2)).toPoint();
        const QPoint to=first->mapToScene(QPointF(first->width()*0.1,first->height()/2)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,from);
        for(int step=1;step<=10;++step) {
            QMouseEvent move(QEvent::MouseMove,QPointF(from+(to-from)*step/10),QPointF(from+(to-from)*step/10),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
            QCoreApplication::sendEvent(window,&move); QTest::qWait(20);
        }
        QCOMPARE(computers->data(computers->index(0),Qt::UserRole+1).toString(),QString("device-b"));
        if(!qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS").isEmpty()) {
            QTest::qWait(250); QVERIFY(window->grabWindow().save(qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS")+"/devices-arrange.png"));
        }
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,to);
        QCOMPARE(recalls,1);
        QVERIFY(QMetaObject::invokeMethod(arrange,"clicked"));
        QVERIFY(!grid->property("arranging").toBool());
        QVERIFY(QMetaObject::invokeMethod(computers,"refreshFavorites"));
        QCOMPARE(computers->data(computers->index(0),Qt::UserRole+1).toString(),QString("device-b"));
        QVERIFY(QMetaObject::invokeMethod(computers,"moveComputer",Q_ARG(int,1),Q_ARG(int,0)));
        QCOMPARE(computers->data(computers->index(0),Qt::UserRole+1).toString(),QString("device-a"));
        // Groups: holding a device over the middle of another device groups them.
        QVERIFY(QMetaObject::invokeMethod(arrange,"clicked"));
        QTest::qWait(150);
        a=findVisual(gridItem,"device-device-a"); b=findVisual(gridItem,"device-device-b"); QVERIFY(a && b);
        {
            auto target=qobject_cast<QQuickItem*>(a), held=qobject_cast<QQuickItem*>(b);
            const QPoint start=held->mapToScene(QPointF(held->width()/2,held->height()/2)).toPoint();
            const QPoint end=target->mapToScene(QPointF(target->width()/2,target->height()/2)).toPoint();
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
            for(int step=1;step<=10;++step) {
                const QPointF at=QPointF(start+(end-start)*step/10);
                QMouseEvent move(QEvent::MouseMove,at,at,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
                QCoreApplication::sendEvent(window,&move); QTest::qWait(20);
            }
            QCOMPARE(grid->property("combineIndex").toInt(),0);
            QCOMPARE(computers->data(computers->index(0),Qt::UserRole+1).toString(),QString("device-a"));
            if(!qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS").isEmpty()) {
                QTest::qWait(200); QVERIFY(window->grabWindow().save(qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS")+"/devices-group-drop.png"));
            }
            QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,end);
        }
        QTRY_COMPARE(computers->rowCount(),1);
        QVERIFY(computers->data(computers->index(0),Qt::UserRole+12).toBool());
        QCOMPARE(computers->data(computers->index(0),Qt::UserRole+14).toInt(),2);
        const QString groupId=computers->data(computers->index(0),Qt::UserRole+13).toString();
        QCOMPARE(grid->property("combineIndex").toInt(),-1);
        QVERIFY(QMetaObject::invokeMethod(arrange,"clicked"));
        QTest::qWait(150);
        auto groupCard=findVisual(gridItem,"group-"+groupId); QVERIFY(groupCard);
        if(!qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS").isEmpty())
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS")+"/devices-group-card.png"));
        QVERIFY(QMetaObject::invokeMethod(groupCard,"clicked"));
        auto folderDialog=grid->findChild<QObject*>("groupFolderDialog"); QVERIFY(folderDialog);
        QTRY_VERIFY(folderDialog->property("visible").toBool());
        QCOMPARE(folderDialog->property("groupId").toString(),groupId);
        auto folderModel=qobject_cast<QAbstractListModel*>(grid->findChild<QObject*>("groupFolderModel")); QVERIFY(folderModel);
        auto editFolder=findVisual(window->contentItem(),"editGroupFolder"); QVERIFY(editFolder);
        QVERIFY(QMetaObject::invokeMethod(editFolder,"clicked"));
        QVERIFY(folderDialog->property("editing").toBool());
        QTest::qWait(150);
        if(!qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS").isEmpty())
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS")+"/devices-group-open.png"));
        QVERIFY(QMetaObject::invokeMethod(folderDialog,"close"));
        QTRY_VERIFY(!folderDialog->property("visible").toBool());
        // Deleting a group keeps its devices: they return where the group was.
        auto groupMenu=grid->findChild<QObject*>("groupMenu"); QVERIFY(groupMenu);
        QVERIFY(groupMenu->setProperty("groupId",groupId));
        auto deleteGroup=grid->findChild<QObject*>("deleteGroup"); QVERIFY(deleteGroup);
        QVERIFY(QMetaObject::invokeMethod(deleteGroup,"triggered"));
        QTRY_COMPARE(computers->rowCount(),2);
        QCOMPARE(computers->data(computers->index(0),Qt::UserRole+1).toString(),QString("device-a"));
        QCOMPARE(computers->data(computers->index(1),Qt::UserRole+1).toString(),QString("device-b"));
        QTest::qWait(150);
        a=findVisual(gridItem,"device-device-a"); b=findVisual(gridItem,"device-device-b"); QVERIFY(a && b);
        // Capture only synthetic pages, never the user's running application.
        const QString shots=qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS");
        if(!shots.isEmpty()) {
            QDir().mkpath(shots);
            // Dismiss the synthetic details dialog before screenshots.
            auto details=grid->findChild<QObject*>("deviceDetails"); QVERIFY(details);
            QVERIFY(QMetaObject::invokeMethod(details,"close"));
            window->resize(800,620); QTest::qWait(400);
            QVERIFY(window->grabWindow().save(shots+"/devices.png"));
            QVERIFY(QMetaObject::invokeMethod(computers,"setShowAdd",Q_ARG(bool,true)));
            QTest::qWait(200); QVERIFY(window->grabWindow().save(shots+"/devices-add.png"));
            QVERIFY(QMetaObject::invokeMethod(computers,"setShowAdd",Q_ARG(bool,false))); QTest::qWait(100);
            // setShowAdd resets the model and destroys its old delegates.
            a=findVisual(gridItem,"device-device-a"); QVERIFY(a);
            auto buttons=a->findChildren<QObject*>();
            for(auto button : buttons) if(button->property("text").toString()=="Return to desktop" && button->property("highlighted").isValid()) {
                auto content=button->property("contentItem").value<QObject*>(); QVERIFY(content);
                QCOMPARE(content->property("color").value<QColor>(), QColor("#ffffff"));
            }
            QVERIFY(QMetaObject::invokeMethod(root.data(),"testSharing"));
            window->resize(640,620); QTest::qWait(400);
            QVERIFY(window->grabWindow().save(shots+"/sharing-narrow.png"));
            QVERIFY(QMetaObject::invokeMethod(root.data(),"testSettings"));
            QTest::qWait(400); QVERIFY(window->grabWindow().save(shots+"/settings-narrow.png"));
            auto settings=root->property("testCurrentPage").value<QObject*>(); QVERIFY(settings);
            auto choice=settings->findChild<QObject*>("themeChoice"); QVERIFY(choice);
            QVERIFY(QMetaObject::invokeMethod(choice,"activated",Q_ARG(int,2)));
            QTest::qWait(100); QVERIFY(window->grabWindow().save(shots+"/settings-dark.png"));
            auto accent=settings->findChild<QObject*>("accentChoice"); QVERIFY(accent);
            QVERIFY(QMetaObject::invokeMethod(accent,"activated",Q_ARG(int,3)));
            QTest::qWait(100); QVERIFY(window->grabWindow().save(shots+"/appearance-dark.png"));
            QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevicesDuringSession"));
            window->resize(1120,760); QTest::qWait(100);
            QVERIFY(window->grabWindow().save(shots+"/devices-dark.png"));
            {
                auto page=root->property("testCurrentPage").value<QObject*>(); QVERIFY(page);
                auto shown=page->property("model").value<QObject*>(); QVERIFY(shown);
                QVERIFY(QMetaObject::invokeMethod(shown,"setShowAdd",Q_ARG(bool,true)));
                QTest::qWait(200); QVERIFY(window->grabWindow().save(shots+"/devices-dark-add.png"));
                QVERIFY(QMetaObject::invokeMethod(shown,"setShowAdd",Q_ARG(bool,false)));
            }
            {
                // The bundled manual: first chapter open, the rest collapsed.
                auto manual=findVisual(window->contentItem(),"manualButton"); QVERIFY(manual);
                QVERIFY(QMetaObject::invokeMethod(manual,"clicked")); QTest::qWait(300);
                auto page=root->property("testCurrentPage").value<QObject*>(); QVERIFY(page);
                QCOMPARE(page->objectName(),QString("Manual"));
                QVERIFY(findVisual(qobject_cast<QQuickItem*>(page),"chapter0"));
                QVERIFY(window->grabWindow().save(shots+"/manual-dark.png"));
                QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevicesDuringSession")); QTest::qWait(150);
            }
            QVERIFY(QMetaObject::invokeMethod(root.data(),"testCards"));
            QTest::qWait(100);
            auto current=root->property("testCurrentPage").value<QObject*>(); QVERIFY(current);
            QVERIFY(!current->property("compact").toBool());
            QVERIFY(window->grabWindow().save(shots+"/cards-dark.png"));
            window->resize(640,620); QTest::qWait(100);
            QVERIFY(!current->property("compact").toBool());
            QTranslator chinese;
            QVERIFY(chinese.load(qEnvironmentVariable("TEST_GUI_DIR")+"/../languages/qml_zh_CN.qm"));
            QVERIFY(QCoreApplication::installTranslator(&chinese)); engine.retranslate();
            QTest::qWait(100); QVERIFY(window->grabWindow().save(shots+"/devices-chinese-narrow.png"));
            QVERIFY(QMetaObject::invokeMethod(root.data(),"testDevice"));
            QTest::qWait(150); QVERIFY(window->grabWindow().save(shots+"/device-settings-chinese.png"));
            auto devicePage=root->property("testCurrentPage").value<QObject*>(); QVERIFY(devicePage);
            auto advanced=devicePage->findChild<QObject*>("deviceAdvancedButton"); QVERIFY(advanced);
            QVERIFY(QMetaObject::invokeMethod(advanced,"clicked"));
            QTest::qWait(200); QVERIFY(window->grabWindow().save(shots+"/device-advanced-chinese.png"));

            QCoreApplication::removeTranslator(&chinese); engine.retranslate();
        }
        {
            // The selection slides to the section the current page belongs to.
            auto selection=findVisual(window->contentItem(),"sectionSelection"); QVERIFY(selection);
            QVERIFY(findVisual(window->contentItem(),"devicesButton"));
            const double devicesX=selection->property("x").toDouble();
            QVERIFY(devicesX > 0.0);
            auto manual=findVisual(window->contentItem(),"manualButton"); QVERIFY(manual);
            QVERIFY(QMetaObject::invokeMethod(manual,"clicked")); QTest::qWait(250);
            auto manualPage=root->property("testCurrentPage").value<QObject*>(); QVERIFY(manualPage);
            QCOMPARE(manualPage->objectName(),QString("Manual"));
            const double manualX=selection->property("x").toDouble();
            QVERIFY(manualX < devicesX);
            auto settings=findVisual(window->contentItem(),"settingsButton"); QVERIFY(settings);
            QVERIFY(QMetaObject::invokeMethod(settings,"clicked")); QTest::qWait(250);
            QVERIFY(selection->property("x").toDouble() > manualX);
            auto devices=findVisual(window->contentItem(),"devicesButton");
            QVERIFY(QMetaObject::invokeMethod(devices,"clicked")); QTest::qWait(250);
            QTRY_COMPARE(selection->property("x").toDouble(),devicesX);
            QVERIFY(!findVisual(window->contentItem(),"backButton"));
        }
        emit session.sessionFinished(0);
        QTRY_COMPARE(root->property("testDepth").toInt(),1);
        QVERIFY(root->property("activeHostId").toString().isEmpty());
        emit session.readyForDeletion(); QTest::qWait(50);
        const auto bad=warnings.filter(QRegularExpression("ReferenceError|TypeError|binding loop|Binding loop|Cannot assign|Unable to assign|Missing parent|Component is not ready"));
        QVERIFY2(bad.isEmpty(),qPrintable(bad.join('\n')));
    }
    void commonLanguagesRetranslateSettings() {
        QTemporaryDir directory;
        HostManager host(nullptr, directory.path());
        PeerManager peers(&host,credential("TEST_CERT_A"),credential("TEST_KEY_A"),directory.path()+"/peers",0,QHostAddress::LocalHost);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("diagnostics", &Diagnostics::instance());
        engine.rootContext()->setContextProperty("hostManager", &host);
        engine.rootContext()->setContextProperty("peerManager", &peers);
        const QString gui=qEnvironmentVariable("TEST_GUI_DIR");
        QQmlComponent themeComponent(&engine,QUrl::fromLocalFile(gui+"/UiTheme.qml"));
        QScopedPointer<QObject> theme(themeComponent.create()); QVERIFY(theme);
        engine.rootContext()->setContextProperty("ui",theme.data());
        QQmlComponent component(&engine,QUrl::fromLocalFile(gui+"/SettingsHome.qml"));
        QScopedPointer<QObject> page(component.create()); QVERIFY2(page,qPrintable(component.errorString()));
        const QString english=page->property("heading").toString();
        QCOMPARE(english,QString("Make DeskPort your own."));
        auto themeChoice=page->findChild<QObject*>("themeChoice"); QVERIFY(themeChoice);
        QVERIFY(!page->findChild<QObject*>("windowModeChoice"));
        for (const auto& language : {"zh_CN","zh_TW","ja","ko","de","fr","es"}) {
            QTranslator translator;
            QVERIFY(translator.load(gui+"/../languages/qml_"+language+".qm"));
            QVERIFY(QCoreApplication::installTranslator(&translator));
            engine.retranslate();
            QVERIFY(page->property("heading").toString()!=english);
            QCOMPARE(themeChoice->property("currentIndex").toInt(),0);
            QVERIFY(!translator.translate("SettingsHome","Follow system").isEmpty());
            QVERIFY(!translator.translate("SettingsHome","Enable diagnostic logs").isEmpty());
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
