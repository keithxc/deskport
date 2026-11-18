#include "trafficbudget.h"
#include "backend/shortcutbinding.h"
#include <QSignalSpy>
#include "diagnostics.h"
#include <QDesktopServices>
#include <algorithm>
#include <cmath>
#include <QtTest>
#include <QRegularExpression>
#include <QDateTime>
#include <QApplication>
#include <QSettings>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QPointer>
#include <QTranslator>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
#include <QAbstractListModel>
#include <QDir>
#include "peermanager.h"
#include "hostlayout.h"
#include "manual.h"
#include "peerstore.h"
#include "singleinstance.h"
#include "../shared/deskport-core/portable/include/deskport/catalog.h"

static QQuickItem* findVisual(QQuickItem* item, const QString& name, bool visibleOnly = false) {
    if (item->objectName() == name && (!visibleOnly || item->isVisible())) return item;
    for (auto child : item->childItems()) if (auto found = findVisual(child, name, visibleOnly)) return found;
    return nullptr;
}
static QByteArray credential(const char* name) {
    QFile f(qEnvironmentVariable(name)); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
class TestGlobalShortcut : public QObject {
    Q_OBJECT
public:
    bool recording = false;
    QPointer<QWindow> recordingWindow;
    Q_INVOKABLE void setRecording(bool value) { recording = value; }
    Q_INVOKABLE void setRecordingWindow(QWindow* window) { recordingWindow = window; }
};
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
class TestPresentation : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString backdrop MEMBER backdrop NOTIFY backdropChanged)
public:
    QString backdrop;
signals:
    void backdropChanged();
};
// QML wiring fixture; the native renderer is exercised by --canvas-self-test.
class TestCanvas : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* presentation MEMBER presentation)
    Q_PROPERTY(bool frameReady MEMBER ready NOTIFY readyChanged)
    Q_PROPERTY(bool transitionWaiting MEMBER waiting)
    Q_PROPERTY(bool textInputEnabled MEMBER textInput)
    Q_PROPERTY(bool cursorHidden MEMBER cursorHidden)
    Q_PROPERTY(bool captureRequested MEMBER capture)
    Q_PROPERTY(bool relativePointer MEMBER relative)
    Q_PROPERTY(QVariantMap pointerRegion MEMBER pointerRegion)
public:
    using QQuickItem::QQuickItem;
    QObject* presentation = nullptr;
    bool ready = false, waiting = false, textInput = false, cursorHidden = false, capture = false, relative = false;
    QVariantMap pointerRegion;
signals:
    void readyChanged();
    void viewportChanged(QJsonObject state);
    void input(QJsonObject event);
    void transitionCancelRequested();
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
    bool quiet = false;
    Q_INVOKABLE bool quietRecovery() const { return quiet; }
    int delay = 0; bool cancelled = false;
    Q_INVOKABLE int retryDelay() const { return delay; }
    Q_INVOKABLE void cancelRecovery() { cancelled = true; }
    int executions = 0;
    QQuickWindow* receivedWindow = nullptr;
    TestSession* next = nullptr;
    bool refuseContinuation = false;
    std::function<void()> duringExec;
    Q_INVOKABLE bool adaptiveRestartPending() const { return next != nullptr; }
    Q_INVOKABLE TestSession* adaptiveContinuation() { auto value = refuseContinuation ? nullptr : next; next = nullptr; return value; }
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
    QStringList devices{"device-a","device-b"};
    Q_INVOKABLE void setDeviceCount(int count) {
        beginResetModel(); devices = {"device-a", "device-b"};
        for (int i=2; i<count; ++i) devices.append(QString("fixture-%1").arg(i));
        endResetModel();
    }
    // Shared by every model instance, like the saved layout, so a group's
    // folder model sees groups made on the device list.
    static inline QVariantList stored; static inline bool hasStored = false;
    static void resetLayout() { stored.clear(); hasStored = false; }
    QString group;
    // Screenshots show the trailing add card; behavior checks run without it.
    bool showAdd = false;
    // The second device's state for screenshots: 0 online, 1 checking, 2 offline.
    int secondState = 0;
    Q_INVOKABLE void setSecondState(int state) { beginResetModel(); secondState = state; endResetModel(); }
    bool hasAdd() const { return showAdd && group.isEmpty(); }
    Q_INVOKABLE void setShowAdd(bool value) { beginResetModel(); showAdd = value; endResetModel(); }
    HostLayout layout() { return HostLayout(stored, hasStored, {}, [this](const QVariantList& items){ stored = items; hasStored = true; }); }
    QVector<HostLayout::Entry> rows() const { return const_cast<TestComputers*>(this)->layout().entries(devices, group); }
    Q_INVOKABLE QString hostIdAt(int index) const { const auto list=rows(); return index>=0 && index<list.size() ? list[index].id : QString(); }
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
        case 1: return entry.id;
        case 2: return first || secondState == 0;
        case 3: case 9: return true;
        case 4: return !first && secondState == 1;
        case 10: return false;
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
    Q_INVOKABLE QString shortcutFromKey(int key, int modifiers) const { return ShortcutBinding::fromEvent(key, modifiers); }
    Q_INVOKABLE QString shortcutDisplay(const QString& value) const { return ShortcutBinding::display(value); }
    Q_INVOKABLE QVariantList shortcutKeys() const {
        QVariantList choices;
        for (const int key : ShortcutBinding::supportedKeys())
            choices.append(QVariantMap{{"key", key}, {"label", QKeySequence(key).toString(QKeySequence::NativeText)}});
        return choices;
    }
    Q_INVOKABLE QVariantMap shortcutParts(const QString& value) const {
        const auto binding = ShortcutBinding::parse(value);
        return {{"key", binding.key}, {"modifiers", int(ShortcutBinding::eventModifiers(binding.modifiers))}};
    }
    Q_PROPERTY(QString globalShortcutSequence READ shortcutSequence WRITE setShortcutSequence NOTIFY shortcutSequenceChanged)
    QString shortcutSequence() const { return m_ShortcutSequence; }
    void setShortcutSequence(const QString& value) { m_ShortcutSequence = value; emit shortcutSequenceChanged(); }
    QString m_ShortcutSequence = ShortcutBinding::defaultSequence();
signals:
    void shortcutSequenceChanged();
public:
    Q_PROPERTY(QVariantList desktopAdjustmentChoices READ adjustmentChoices CONSTANT)
    Q_PROPERTY(QStringList desktopAdjustmentLabels READ adjustmentLabels CONSTANT)
    Q_PROPERTY(QVariantList sharedLanguages READ sharedLanguages CONSTANT)
    QVariantList sharedLanguages() const {
        QFile file(qEnvironmentVariable("TEST_GUI_DIR")+"/../../shared/deskport-core/i18n/languages.json");
        if (!file.open(QIODevice::ReadOnly)) return {};
        const QList<int> values = {LANG_EN, LANG_ZH_CN, LANG_ZH_TW, LANG_JA, LANG_FR, LANG_DE, LANG_AR, LANG_KO, LANG_PT, LANG_ES, LANG_BO, LANG_IT, LANG_RU, LANG_ID, LANG_TR, LANG_VI, LANG_TH, LANG_NL, LANG_PL, LANG_UK, LANG_HI, LANG_MS};
        QVariantList result;
        int index = 0;
        for (const auto &entry : QJsonDocument::fromJson(file.readAll()).array())
            result.append(QVariantMap{{"label", entry.toObject().value("name").toString()}, {"value", values.at(index++)}});
        return result;
    }
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
    enum Language { LANG_AUTO, LANG_EN, LANG_FR, LANG_ZH_CN, LANG_DE, LANG_NB_NO, LANG_RU, LANG_ES, LANG_JA, LANG_VI, LANG_TH, LANG_KO, LANG_HU, LANG_NL, LANG_SV, LANG_TR, LANG_UK, LANG_ZH_TW, LANG_PT, LANG_PT_BR, LANG_EL, LANG_IT, LANG_HI, LANG_PL, LANG_CS, LANG_HE, LANG_CKB, LANG_LT, LANG_ET, LANG_AR, LANG_BO, LANG_ID, LANG_MS };
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
// Session fixtures already have saved devices. Avoid writing shared setup
// preferences, which would change subsequent first-run navigation tests.
class SessionPreviewHost : public PreviewHost {
    Q_OBJECT
    Q_PROPERTY(bool setupComplete READ sessionSetupComplete CONSTANT)
public:
    using PreviewHost::PreviewHost;
    bool sessionSetupComplete() const { return true; }
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
        // Reporting first explains, in a panel, that GitHub issues are public.
        QDesktopServices::setUrlHandler("https",this,"captureFeedback");
        QVERIFY(QMetaObject::invokeMethod(button,"clicked"));
        auto notice=page->findChild<QObject*>("feedbackNotice"); QVERIFY(notice);
        QTRY_VERIFY(notice->property("visible").toBool());
        QVERIFY(notice->property("text").toString().contains("public"));
        QVERIFY(!QFile::exists(logs.bundlePath()));
        QVERIFY(QMetaObject::invokeMethod(notice,"accept"));
        QVERIFY(QFile::exists(logs.bundlePath()));
        QTRY_VERIFY(!notice->property("visible").toBool()); QTest::qWait(100);
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
        qmlRegisterType<TestCanvas>("DeskPort.Canvas",1,0,"SessionCanvas");
        qmlRegisterType<TestComputers>("ComputerModel",1,0,"ComputerModel");
        qmlRegisterSingletonType<QObject>("AutoUpdateChecker",1,0,"AutoUpdateChecker",+[](QQmlEngine*,QJSEngine*) -> QObject* { return new TestUpdates; });
        qmlRegisterType<TestSession>("Session",1,0,"Session");
        qmlRegisterSingletonType<Manual>("Manual",1,0,"Manual",+[](QQmlEngine*,QJSEngine*) -> QObject* { return new Manual; });
        qmlRegisterType<TestDesktopApps>("AppModel",1,0,"AppModel");
        qmlRegisterSingletonType<QObject>("SdlGamepadKeyNavigation",1,0,"SdlGamepadKeyNavigation",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine); c.setData("import QtQuick 2.9; QtObject { property int enables: 0; function enable() { enables++ } function disable() {} function getConnectedGamepads() { return 0 } }",QUrl()); return c.create();
        });
        qmlRegisterSingletonType<QObject>("ComputerManager",1,0,"ComputerManager",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine); c.setData("import QtQuick 2.9; QtObject { signal quitAppCompleted(var error); signal computerAddCompleted(bool success, bool blocked); function startPolling() {} function stopPollingAsync() {} function addBoundHost(peer, explicitAdd) {} function deleteHostById(hostId) { return true } }",QUrl()); return c.create();
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
 property int uiAccent: 1; property bool showTraffic: true; property bool hideStreamTitleBar: false; property int uiTheme: 0; property bool compactDevices: true; property int uiDisplayMode: 0
 property bool globalShortcutEnabled: true
 property int language: 1; property int retranslations: 0
 function retranslate() { retranslations++; return true }
 function manualLanguage() { return "en" }
 property int width: 2048; property int height: 1152; property int fps: 75; property int bitrateKbps: 125000
 property int windowMode: 2; property int recommendedFullScreenMode: 1; property int captureSysKeysMode: 1; property int saves: 0
 property bool smartStreaming: true; property bool framePacing: false; property bool showPerformanceOverlay: false
 property bool localTextInput: false; property bool sharedClipboard: false; property bool showLocalCursor: true; property bool adaptiveResolution: true; property bool enableVsync: true; property bool absoluteMouseMode: true; property bool reverseScrollDirection: false
 property bool muteOnFocusLoss: true; property bool playAudioOnHost: false; property bool enableMdns: true; property bool keepAwake: true
 function forDevice(id) { deviceId = id; return this }
 function save() { saves++ }
})",QUrl()); return c.create();
        });
        qmlRegisterSingletonType<QObject>("SystemProperties",1,0,"SystemProperties",+[](QQmlEngine* engine,QJSEngine*) -> QObject* {
            QQmlComponent c(engine); c.setData("import QtQuick 2.9; QtObject { property bool systemDark: false; property color systemAccent: \"#3269d7\"; property bool hasBrowser: false; property bool hasDesktopEnvironment: true; property bool isWow64: false; property bool hasHardwareAcceleration: true; property bool isRunningWayland: false; property bool isRunningXWayland: false; property string unmappedGamepads: \"\"; property string friendlyNativeArchName: \"test\"; property string versionString: \"test\"; property int memorySamples: 0; function memoryUsage(pid) { memorySamples++; return {available: true, complete: true, client: 10485760, host: 0, helpers: 0, total: 10485760} } }",QUrl()); return c.create();
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
        QCOMPARE(root->property("currentPage").value<QObject*>(), page);
        QVERIFY(QMetaObject::invokeMethod(root.data(),"back"));
        QVERIFY(root->property("navigationVisible").toBool());
        QVERIFY(QMetaObject::invokeMethod(root.data(),"startQuit"));
        QTRY_COMPARE(root->property("quits").toInt(),1);
        QVERIFY(!root->property("navigationVisible").toBool());
        QVERIFY(QMetaObject::invokeMethod(root.data(),"back"));
        QVERIFY(root->property("navigationVisible").toBool());
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
    }
    void lifecycleBackoffAndFailureDoNotShowShell() {
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        TestSession session; session.delay = 10000; session.quiet = true;
        engine.rootContext()->setContextProperty("testSession", &session);
        QQmlComponent harness(&engine);
        harness.setData(R"(import QtQuick 2.9
import QtQuick.Controls 2.2
ApplicationWindow {
 id: window; width: 800; height: 600; visible: false
 property alias depth: stackView.depth
 property int errorsOpened: streamSegueErrorDialog.opens
 QtObject { id: streamSegueErrorDialog; property string text: ""; property bool quitAfter: false; property int opens: 0; function open() { opens++ } }
 StackView { id: stackView; anchors.fill: parent; initialItem: Item {} }
 function start() { stackView.push(Qt.resolvedUrl("StreamSegue.qml"), {session: testSession, appName: "Test"}, StackView.Immediate) }
})",QUrl::fromLocalFile(qEnvironmentVariable("TEST_GUI_DIR")+"/quiet-lifecycle-harness.qml"));
        QScopedPointer<QObject> root(harness.create()); QVERIFY2(root,qPrintable(harness.errorString()));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"start"));
        QTest::qWait(100);
        QCOMPARE(session.executions,0); QVERIFY(!root->property("visible").toBool());
        emit session.stageFailed("fixture",-1,{});
        TestSession withdrawn;
        session.next = &withdrawn; session.refuseContinuation = true;
        emit session.sessionFinished(0);
        QCOMPARE(root->property("depth").toInt(),2);
        emit session.readyForDeletion(); // Revocation wins before continuation creation.
        QTest::qWait(100);
        QCOMPARE(root->property("depth").toInt(),1); QVERIFY(!root->property("visible").toBool());
        QCOMPARE(root->property("errorsOpened").toInt(),0);
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
    void controlCenterSurvivesTransportContinuation() {
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
        auto controlPage = root->property("currentPage").value<QObject*>();
        TestSession next;
        QQmlEngine::setObjectOwnership(&next, QQmlEngine::CppOwnership);
        session.next = &next;
        emit session.sessionFinished(0);
        emit session.readyForDeletion();
        QTRY_COMPARE(next.executions, 1);
        QCOMPARE(root->property("depth").toInt(), 3);
        QCOMPARE(root->property("currentPage").value<QObject*>(), controlPage);
        next.makeViewerReady();
        QVERIFY(root->property("visible").toBool());
        QCOMPARE(session.executions, 1);
        emit next.sessionFinished(0);
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
    void browserPairingRemovalKeepsOtherBrowsersAndReportsFailure() {
        QTemporaryDir dir;
        HostManager host(nullptr, dir.path()+"/host");
        PeerManager peers(&host, credential("TEST_CERT_A"), credential("TEST_KEY_A"), dir.path()+"/peers", 0, QHostAddress::LocalHost);
        QQmlEngine engine;
        const QString gui = qEnvironmentVariable("TEST_GUI_DIR");
        QQmlComponent themeComponent(&engine, QUrl::fromLocalFile(gui+"/UiTheme.qml"));
        QScopedPointer<QObject> theme(themeComponent.create()); QVERIFY(theme);
        QQmlComponent fixture(&engine);
        fixture.setData(R"(import QtQuick 2.9
QtObject {
 property string accessCode: "123456"
 property real accessCodeExpiresAt: Date.now() + 17500
 property real codeLockedUntil: 0
 property int qrRequests: 0
 property int resets: 0
 function authenticatorQr() { qrRequests += 1; return "data:image/svg+xml;base64,PHN2ZyB4bWxucz0naHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmcnIHZpZXdCb3g9JzAgMCAxIDEnLz4=" }
 function resetAuthenticator() { resets += 1; return true }
 property bool listening: true
 property bool tailnetListening: true
 property bool tailnetIdentity: true
 property string qrSource: ""
 property string errorString: ""
 property var urls: []
 property var pairedBrowsers: [{id: "one", name: "Chrome fixture"}, {id: "two", name: "Safari fixture"}]
 property string removed: ""
 property bool writable: false
 function revokeBrowser(id) {
   removed = id
   if (!writable) return false
   pairedBrowsers = pairedBrowsers.filter(function(browser) { return browser.id !== id })
   return true
 }
})", QUrl());
        QScopedPointer<QObject> browser(fixture.create()); QVERIFY2(browser, qPrintable(fixture.errorString()));
        browser->setProperty("urls", QStringList{"https://example.invalid:48992/"});
        engine.rootContext()->setContextProperty("ui", theme.data());
        engine.rootContext()->setContextProperty("hostManager", &host);
        engine.rootContext()->setContextProperty("peerManager", &peers);
        engine.rootContext()->setContextProperty("browserHost", browser.data());
        engine.rootContext()->setContextProperty("diagnostics", &Diagnostics::instance());
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& errors) {
            for (const auto& error : errors) warnings << error.toString();
        });
        QQmlComponent component(&engine, QUrl::fromLocalFile(gui+"/HostView.qml"));
        QScopedPointer<QObject> page(component.create()); QVERIFY2(page, qPrintable(component.errorString()));
        auto item = qobject_cast<QQuickItem*>(page.data()); QVERIFY(item);
        QQuickWindow window; window.resize(820, 1100);
        item->setParentItem(window.contentItem()); item->setSize(QSizeF(820, 1100));
        auto details = findVisual(item, "browserAccessDetails"); QVERIFY(details);
        details->setProperty("checked", true); window.show(); QTest::qWait(80);
        // The access code stays hidden until the operator asks to see it.
        auto code = findVisual(item, "browserAccessCode"); QVERIFY(code);
        QVERIFY(!code->property("text").toString().contains("123456"));
        auto showCode = findVisual(item, "browserAccessShowCode"); QVERIFY(showCode);
        showCode->setProperty("checked", true);
        QTRY_COMPARE(code->property("text").toString(), QString("123456"));
        showCode->setProperty("checked", false);
        QTRY_VERIFY(!code->property("text").toString().contains("123456"));
        // The 30-second code shows its remaining time; lockout is announced.
        auto countdown = findVisual(item, "browserAccessCodeCountdown"); QVERIFY(countdown);
        QTRY_VERIFY(QRegularExpression("\\b1[5-8]\\b").match(countdown->property("text").toString()).hasMatch());
        auto locked = findVisual(item, "browserAccessCodeLocked"); QVERIFY(locked);
        QVERIFY(!locked->property("visible").toBool());
        browser->setProperty("codeLockedUntil", double(QDateTime::currentMSecsSinceEpoch() + 600000));
        QTRY_VERIFY(locked->property("visible").toBool());
        // The authenticator QR carries the secret, so it is generated only on request.
        auto authenticator = findVisual(item, "browserAuthenticatorToggle"); QVERIFY(authenticator);
        auto qr = findVisual(item, "browserAuthenticatorQr"); QVERIFY(qr);
        QCOMPARE(browser->property("qrRequests").toInt(), 0); QVERIFY(!qr->property("visible").toBool());
        authenticator->setProperty("checked", true);
        QTRY_VERIFY(qr->property("visible").toBool()); QCOMPARE(browser->property("qrRequests").toInt(), 1);
        auto reset = findVisual(item, "browserAuthenticatorReset"); QVERIFY(reset);
        QVERIFY(QMetaObject::invokeMethod(reset, "clicked"));
        QCOMPARE(browser->property("resets").toInt(), 1); QCOMPARE(browser->property("qrRequests").toInt(), 2);
        authenticator->setProperty("checked", false);
        QTRY_VERIFY(!qr->property("visible").toBool());
        auto state = findVisual(item, "browserAccessState"); QVERIFY(state);
        QVERIFY(state->property("text").toString().contains("Tailscale"));
        auto first = findVisual(item, "revokeBrowserPairing-one"); QVERIFY(first);
        QVERIFY(findVisual(item, "revokeBrowserPairing-two"));
        const auto screenshot = qEnvironmentVariable("DESKPORT_PAIRING_UI_SHOT");
        if (!screenshot.isEmpty()) QVERIFY(window.grabWindow().save(screenshot));
        QVERIFY(QMetaObject::invokeMethod(first, "clicked"));
        QCOMPARE(browser->property("removed").toString(), QString("one"));
        QVERIFY(!page->property("browserPairingError").toString().isEmpty());
        QVERIFY(findVisual(item, "revokeBrowserPairing-one"));
        browser->setProperty("writable", true);
        QVERIFY(QMetaObject::invokeMethod(first, "clicked"));
        QTRY_VERIFY(!findVisual(item, "revokeBrowserPairing-one"));
        QVERIFY(findVisual(item, "revokeBrowserPairing-two"));
        QVERIFY(page->property("browserPairingError").toString().isEmpty());
        QVERIFY(!host.running()); QVERIFY(peers.peers().isEmpty());
        const auto bad = warnings.filter(QRegularExpression("ReferenceError|TypeError|binding loop|Binding loop|Cannot assign|Unable to assign"));
        QVERIFY2(bad.isEmpty(), qPrintable(bad.join('\n')));
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
                QVERIFY(QMetaObject::invokeMethod(traffic,"switched",Q_ARG(bool,false)));
                QVERIFY(!prefs->property("showTraffic").toBool());
                // Off by default; the information button explains it without toggling it.
                auto titleBar=page->findChild<QObject*>("hideStreamTitleBarSwitch"); QVERIFY(titleBar);
                QVERIFY(!titleBar->property("on").toBool());
                auto titleBarInfo=page->findChild<QObject*>("hideStreamTitleBarSwitchInfo"); QVERIFY(titleBarInfo);
                QVERIFY(titleBarInfo->property("visible").toBool());
                auto titleBarPanel=page->findChild<QObject*>("titleBarPanel"); QVERIFY(titleBarPanel);
                QVERIFY(QMetaObject::invokeMethod(titleBarInfo,"clicked"));
                QTRY_VERIFY(titleBarPanel->property("visible").toBool());
                QCOMPARE(titleBarPanel->property("paragraphs").toList().size(),3);
                QVERIFY(!titleBar->property("detail").toString().isEmpty());
                QCOMPARE(titleBarPanel->property("paragraphs").toList().at(1).toString(),
                         titleBar->property("detail").toString());
                QVERIFY(!prefs->property("hideStreamTitleBar").toBool());
                QVERIFY(QMetaObject::invokeMethod(titleBarPanel,"close"));
                QVERIFY(QMetaObject::invokeMethod(titleBar,"switched",Q_ARG(bool,true)));
                QVERIFY(prefs->property("hideStreamTitleBar").toBool());
                QTRY_VERIFY(titleBar->property("on").toBool());
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
        auto windowChoice=page->findChild<QObject*>("windowModeChoice"); QVERIFY(windowChoice);
        QVERIFY(QMetaObject::invokeMethod(windowChoice,"activated",Q_ARG(int,1)));
        QCOMPARE(prefs->property("windowMode").toInt(),1);
        QVERIFY(QMetaObject::invokeMethod(windowChoice,"activated",Q_ARG(int,2)));
        QCOMPARE(prefs->property("windowMode").toInt(),2);
        // Basic switches: fit to window, sound and input.
        auto fit=page->findChild<QObject*>("deviceFitWindow"); QVERIFY(fit);
        const bool adaptive=prefs->property("adaptiveResolution").toBool();
        QVERIFY(QMetaObject::invokeMethod(fit,"clicked"));
        QCOMPARE(prefs->property("adaptiveResolution").toBool(),!adaptive);
        // Advanced streaming settings expand in place; nothing is pushed.
        auto advanced=page->findChild<QObject*>("deviceAdvancedButton"); QVERIFY(advanced);
        auto section=page->findChild<QObject*>("deviceAdvanced"); QVERIFY(section);
        QVERIFY(!section->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(advanced,"clicked"));
        QVERIFY(page->property("advancedOpen").toBool());
        auto ime = page->findChild<QObject*>("localTextInputSwitch"); QVERIFY(ime);
        auto systemKeys = page->findChild<QObject*>("systemKeysChoice"); QVERIFY(systemKeys);
        const int savedKeysMode = prefs->property("captureSysKeysMode").toInt();
        QVERIFY(QMetaObject::invokeMethod(ime, "switched", Q_ARG(bool, true)));
        QVERIFY(prefs->property("localTextInput").toBool());
        QVERIFY(!systemKeys->property("enabled").toBool());
        QCOMPARE(prefs->property("captureSysKeysMode").toInt(), savedKeysMode);
        QVERIFY(QMetaObject::invokeMethod(ime, "switched", Q_ARG(bool, false)));
        QVERIFY(systemKeys->property("enabled").toBool());
    }
    void managedSessionNavigation() {
        TestComputers::resetLayout();
        QTemporaryDir directory;
        SessionPreviewHost host(nullptr,directory.path()+"/host");
        PeerManager peers(&host,credential("TEST_CERT_A"),credential("TEST_KEY_A"),directory.path()+"/peers",0,QHostAddress::LocalHost);
        QQmlEngine engine;
        QQmlComponent fixture(&engine);
        fixture.setData(R"(import QtQuick 2.9
QtObject {
 property bool busy: true
 property bool canvasEnabled: true
 property var frameWindow: null
 function trackCanvasWindowFrame(window) { frameWindow = window }
 function setCanvasWindowFrameless(hidden) {
   if (frameWindow) frameWindow.flags = hidden ? (Qt.Window | Qt.FramelessWindowHint) : Qt.Window
 }
 property bool canvasVisible: false
 property var canvas: ({})
 property var presentation: null
 property int answer: -1
 function updateCanvas(state) {}
 function canvasInput(event) {}
 function answerCanvasQuestion(choice) { answer=choice }
 property string selectedId: "device-a"
 property var selectedTraffic: ({received:100, sent:20})
 property int listRequests: 0
 property string disconnected: ""
 property string reconnected: ""
 property string fullscreenId: ""
 property var states: ({"device-a":"connected", "device-b":"connected"})
 function open(id, name, address) { select(id) }
 function reconnect(id) { reconnected=id }
 function fullscreen(id) { fullscreenId=id }
 function setSecondState(state) { states={"device-a":"connected", "device-b":state} }
 property var sessions: [ {id:"device-a", name:"First desktop", state:"connected", selected:true, error:""}, {id:"device-b", name:"Second desktop", state:"connected", selected:false, error:""} ]
 function setFirstError(message) {
   states={"device-a":"error", "device-b":"connected"}
   sessions=[{id:"device-a", name:"First desktop", state:"error", selected:true, error:message}, {id:"device-b", name:"Second desktop", state:"connected", selected:false, error:""}]
 }
 function restoreFirst() { states={"device-a":"connected", "device-b":"connected"} }
 function select(id) { selectedId=id }
 property int recalls: 0
 function recall() { recalls++; select(selectedId); return true }
 function showDevices() { listRequests++ }
 function disconnectSession(id) { disconnected=id }
})",QUrl());
        QScopedPointer<QObject> manager(fixture.create()); QVERIFY(manager);
        engine.rootContext()->setContextProperty("sessionManager",manager.data());
        engine.rootContext()->setContextProperty("diagnostics", &Diagnostics::instance());
        engine.rootContext()->setContextProperty("hostManager", &host);
        engine.rootContext()->setContextProperty("peerManager", &peers);
        engine.rootContext()->setContextProperty("initialView", QString("qrc:/gui/PcView.qml"));
        engine.rootContext()->setContextProperty("startInBackground", true);
        engine.rootContext()->setContextProperty("startSharingPage", false);
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        QQmlComponent component(&engine,QUrl("qrc:/gui/main.qml"));
        QScopedPointer<QObject> root(component.create()); QVERIFY2(root,qPrintable(component.errorString()));
        auto window=qobject_cast<QQuickWindow*>(root.data()); QVERIFY(window);
        manager->setProperty("frameWindow", QVariant::fromValue<QObject*>(window));
        window->resize(800,620); window->show(); QTest::qWait(100);
        QVERIFY(!findVisual(window->contentItem(),"sessionStrip"));
        QVERIFY(!findVisual(window->contentItem(),"sessionSelect-b"));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"recallRemoteSession"));
        QCOMPARE(manager->property("recalls").toInt(),1);
        QCOMPARE(manager->property("selectedId").toString(),QString("device-a"));
        QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevices"));
        QCOMPARE(manager->property("listRequests").toInt(),1);
        QCOMPARE(manager->property("sessions").value<QJSValue>().toVariant().toList().size(),2);
        QVERIFY(!findVisual(window->contentItem(),"sessionDisconnect-a"));
        QCOMPARE(manager->property("disconnected").toString(),QString());
        auto first=findVisual(window->contentItem(),"device-device-a",true); QVERIFY(first);
        auto second=findVisual(window->contentItem(),"device-device-b",true); QVERIFY(second);
        auto firstCard=first->property("contentItem").value<QObject*>(); QVERIFY(firstCard);
        auto secondCard=second->property("contentItem").value<QObject*>(); QVERIFY(secondCard);
        QCOMPARE(firstCard->property("actionText").toString(),QString("Return to desktop"));
        QCOMPARE(secondCard->property("actionText").toString(),QString("Return to desktop"));
        for (auto card : {secondCard,firstCard,secondCard}) {
            QVERIFY(QMetaObject::invokeMethod(card,"activateRequested"));
            QCOMPARE(manager->property("selectedId").toString(), card == firstCard ? QString("device-a") : QString("device-b"));
        }
        auto firstPanel=first->findChild<QObject*>("devicePanel-device-a"); QVERIFY(firstPanel);
        QVERIFY(QMetaObject::invokeMethod(manager.data(),"setFirstError",Q_ARG(QVariant,QString("Canvas fixture failed"))));
        QTRY_COMPARE(firstPanel->property("sessionError").toString(),QString("Canvas fixture failed"));
        auto failureLabel=firstPanel->findChild<QObject*>("panelSessionError"); QVERIFY(failureLabel);
        QCOMPARE(failureLabel->property("text").toString(),QString("Canvas fixture failed"));
        QVERIFY(QMetaObject::invokeMethod(manager.data(),"restoreFirst"));
        // A background card's actions must never affect the selected B session.
        QVERIFY(QMetaObject::invokeMethod(firstPanel,"reconnectRequested"));
        QCOMPARE(manager->property("reconnected").toString(),QString("device-a"));
        QVERIFY(QMetaObject::invokeMethod(firstPanel,"fullscreenRequested"));
        QCOMPARE(manager->property("fullscreenId").toString(),QString("device-a"));
        QVERIFY(QMetaObject::invokeMethod(firstPanel,"disconnectRequested"));
        QCOMPARE(manager->property("disconnected").toString(),QString("device-a"));
        QCOMPARE(manager->property("selectedId").toString(),QString("device-b"));
        for (const auto& state : {"starting", "stopping", "error", "connected"}) {
            QVERIFY(QMetaObject::invokeMethod(manager.data(),"setSecondState",Q_ARG(QVariant,QString(state))));
            QCOMPARE(secondCard->property("activeSession").toBool(),QString(state)=="connected");
            QCOMPARE(root->property("activeHostId").toString(),QString(state)=="connected" ? QString("device-b") : QString());
            QVERIFY(firstCard->property("activeSession").toBool());
            QVERIFY(!secondCard->property("statusText").toString().isEmpty());
        }
        const QString shots=qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS");
        if (!shots.isEmpty()) { QTest::qWait(250); QDir().mkpath(shots); QVERIFY(window->grabWindow().save(shots+"/multiple-sessions.png")); }
#ifdef Q_OS_LINUX
        QQmlComponent settingsAccess(&engine);
        settingsAccess.setData(R"(import QtQuick 2.9
import StreamingPreferences 1.0
import SystemProperties 1.0
QtObject {
 property var prefs: StreamingPreferences
 property var system: SystemProperties
})", QUrl());
        QScopedPointer<QObject> settings(settingsAccess.create()); QVERIFY(settings);
        auto preferences = settings->property("prefs").value<QObject*>(); QVERIFY(preferences);
        auto system = settings->property("system").value<QObject*>(); QVERIFY(system);
        system->setProperty("isRunningWayland", true);
        preferences->setProperty("hideStreamTitleBar", true);
        QTRY_VERIFY(window->flags().testFlag(Qt::FramelessWindowHint));
        const auto framelessRoot = window->winId();
        for (int i = 0; i < 10; ++i) {
            manager->setProperty("canvasVisible", i % 2 == 0);
            QTRY_VERIFY(window->flags().testFlag(Qt::FramelessWindowHint));
            QCOMPARE(window->winId(), framelessRoot);
        }
        // XWayland keeps the X11 behavior: only the remote canvas is frameless.
        system->setProperty("isRunningXWayland", true);
        QTRY_VERIFY(!window->flags().testFlag(Qt::FramelessWindowHint));
        manager->setProperty("canvasVisible", true);
        QTRY_VERIFY(window->flags().testFlag(Qt::FramelessWindowHint));
        preferences->setProperty("hideStreamTitleBar", false);
        QTRY_VERIFY(!window->flags().testFlag(Qt::FramelessWindowHint));
        manager->setProperty("canvasVisible", false);
        system->setProperty("isRunningWayland", false);
        system->setProperty("isRunningXWayland", false);
#endif
        // Connection progress and confirmation replace content in this same root.
        const auto nativeRoot = window->winId();
        auto topBar = findVisual(window->contentItem(), "topBar", true); QVERIFY(topBar);
        TestPresentation retainedPresentation;
        retainedPresentation.backdrop = QStringLiteral("data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg==");
        manager->setProperty("presentation", QVariant::fromValue(&retainedPresentation));
        manager->setProperty("canvasVisible", true);
        QTRY_VERIFY(findVisual(window->contentItem(), "sessionCanvas", true));
        QTRY_VERIFY(!topBar->isVisible());
        auto canvasPage = findVisual(window->contentItem(), "sessionCanvasPage", true); QVERIFY(canvasPage);
        auto verifyCanvasBounds = [&] {
            QCOMPARE(canvasPage->mapToScene(QPointF()), QPointF());
            QCOMPARE(canvasPage->size(), window->contentItem()->size());
            QVERIFY(!topBar->isVisible());
            QCOMPARE(window->winId(), nativeRoot);
        };
        verifyCanvasBounds();
        window->resize(1040,740);
        QTRY_COMPARE(canvasPage->width(), qreal(1040));
        verifyCanvasBounds();
        QVERIFY(findVisual(window->contentItem(), "canvasCancelConnection", true));
        QVariantMap question{{"id",1},{"title","Fixture host in use"},{"text","Continue?"},{"cancel","Cancel"},{"accept","Continue"}};
        manager->setProperty("canvas", QVariantMap{{"question",question}});
        QTRY_VERIFY(findVisual(window->contentItem(), "canvasConfirmConnection", true));
        verifyCanvasBounds();
        auto confirm = findVisual(window->contentItem(), "canvasConfirmConnection", true);
        QVERIFY(QMetaObject::invokeMethod(confirm, "clicked"));
        QCOMPARE(manager->property("answer").toInt(),1);
        manager->setProperty("canvas", QVariantMap{});
        if (!shots.isEmpty()) { QTest::qWait(250); QVERIFY(window->grabWindow().save(shots+"/edge-to-edge-connecting.png")); }
        QPointer<QQuickItem> retainedCanvas = findVisual(window->contentItem(), "sessionCanvas", true);
        QVERIFY(retainedCanvas);
        retainedCanvas->setProperty("frameReady", true);
        QTRY_VERIFY(!findVisual(window->contentItem(), "canvasCancelConnection", true));
        auto backdrop = findVisual(window->contentItem(), "canvasTransitionBackdrop");
        QVERIFY(backdrop);
        QVERIFY(!backdrop->isVisible());
        // Prepare the picture while live video is still visible, so showing
        // the reconnect overlay does not wait for PNG decode on the UI path.
        QTRY_COMPARE(backdrop->property("status").toInt(), 1); // Image.Ready
        manager->setProperty("canvas", QVariantMap{{"waiting",true}});
        QTRY_VERIFY(backdrop->isVisible());
        QTRY_VERIFY(findVisual(window->contentItem(), "canvasCancelConnection", true));
        QVERIFY(backdrop->property("source").toUrl().toString().startsWith("data:image/png;base64,"));
        QTRY_COMPARE(backdrop->property("status").toInt(), 1); // Image.Ready: the in-memory PNG decoded.
        manager->setProperty("canvas", QVariantMap{});
        QTRY_VERIFY(!backdrop->isVisible());
        manager->setProperty("canvasVisible", false);
        QTRY_VERIFY(!findVisual(window->contentItem(), "sessionCanvas", true));
        QVERIFY(retainedCanvas); // Hidden, not destroyed with its decoder.
        QTRY_VERIFY(topBar->isVisible());
        manager->setProperty("canvasVisible", true);
        QTRY_COMPARE(findVisual(window->contentItem(), "sessionCanvas", true), retainedCanvas.data());
        QVERIFY(retainedCanvas->property("frameReady").toBool());
        QVERIFY(!findVisual(window->contentItem(), "canvasCancelConnection", true));
        QVERIFY(!topBar->isVisible());
        manager->setProperty("canvasVisible", false);
        manager->setProperty("presentation", QVariant::fromValue<QObject*>(nullptr));
        QTRY_VERIFY(!retainedCanvas); // Ending a session releases its canvas.
        QCOMPARE(window->winId(),nativeRoot);
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
    }
    void navigationAndDeviceIdentity_data() {
        QTest::addColumn<QString>("outcome");
        for (auto name : {"streaming", "failure", "cancel"}) QTest::newRow(name) << QString(name);
    }
    void navigationAndDeviceIdentity() {
        QFETCH(QString, outcome);
        TestComputers::resetLayout();
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
        TrafficBudget budget(nullptr, false);
        budget.today = [] { return QStringLiteral("2026-10-15"); };
        engine.rootContext()->setContextProperty("trafficBudget", &budget);
        QFile source(qEnvironmentVariable("TEST_GUI_DIR")+"/main.qml"); QVERIFY(source.open(QIODevice::ReadOnly));
        auto qml=source.readAll();
        qml.insert(qml.lastIndexOf('}'), R"(
 function testStart() { stackView.push("qrc:/gui/StreamSegue.qml", {session:testSession, appName:"Desktop"}, StackView.Immediate) }
 function testSettings() { showDevices(); navigateTo("qrc:/gui/SettingsHome.qml", "SettingsHome") }
 function testSharing() { showDevices(); navigateTo("qrc:/gui/HostView.qml", "HostView") }
 function testBinding() { showDevices(); navigateTo("qrc:/gui/BindView.qml", "BindView") }
 function testRemove() { var p=stackView.currentItem; p.confirmRemove(p.computerModel,1,"Offline fixture") }
 function testGrid() { return stackView.currentItem }
 function testCards() { StreamingPreferences.compactDevices = false }
 function testSameSettings() { navigateTo("qrc:/gui/SettingsHome.qml", "SettingsHome") }
 function testTheme(mode) { StreamingPreferences.uiTheme = mode }
 property alias testDepth: stackView.depth
 property alias testCurrentPage: stackView.currentItem
)");
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        QQmlComponent component(&engine); component.setData(qml,QUrl("qrc:/gui/main.qml"));
        QScopedPointer<QObject> root(component.create()); QVERIFY2(root,qPrintable(component.errorString()));
        auto window=qobject_cast<QQuickWindow*>(root.data()); QVERIFY(window);
        window->resize(1120,620); window->show(); QTest::qWait(100);
#ifdef Q_OS_MACOS
        if (outcome == "cancel") {
            struct RestoreDirection {
                Qt::LayoutDirection value;
                ~RestoreDirection() { QGuiApplication::setLayoutDirection(value); }
            } restore{QGuiApplication::layoutDirection()};
            auto bar = findVisual(window->contentItem(), "titlebarContent"); QVERIFY(bar);
            const double originalInset = root->property("windowButtonsEnd").toDouble();
            for (double inset : {originalInset, 129.0}) {
                root->setProperty("windowButtonsEnd", inset);
                for (auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
                    QGuiApplication::setLayoutDirection(direction);
                    QTRY_VERIFY2(std::abs(bar->x() - inset - 16) < 0.5,
                        qPrintable(QString("Physical left titlebar clearance: %1, expected %2").arg(bar->x()).arg(inset + 16)));
                }
            }
            root->setProperty("windowButtonsEnd", originalInset);
        }
#endif
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
        auto addDevicePopup=root->findChild<QObject*>("addDeviceDialog"); QVERIFY(addDevicePopup);
        QTRY_VERIFY(addDevicePopup->property("visible").toBool());
        QCOMPARE(root->property("testDepth").toInt(),1);
        QCOMPARE(root->property("testCurrentPage").value<QObject*>()->objectName(),QString("Devices"));
        QVERIFY(addDevicePopup->property("modal").toBool());
        QVERIFY(addDevicePopup->property("width").toReal() <= 460);
        QTest::qWait(200);
        if(!qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS").isEmpty())
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS")+"/add-device-popup.png"));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,QPoint(5,window->height()/2));
        QTRY_VERIFY(!addDevicePopup->property("visible").toBool());
        QCOMPARE(root->property("testDepth").toInt(),1);
        QVERIFY(QMetaObject::invokeMethod(root.data(),"testBinding"));
        QTRY_VERIFY(addDevicePopup->property("visible").toBool());
        const QVariantMap newlyBound{{"name","New computer"}};
        QVERIFY(QMetaObject::invokeMethod(&peers,"peerBound",Q_ARG(QVariantMap,newlyBound),Q_ARG(bool,true)));
        QTRY_VERIFY(!addDevicePopup->property("visible").toBool());
        QTRY_COMPARE(root->property("testDepth").toInt(),1);
        QCOMPARE(root->property("testCurrentPage").value<QObject*>()->objectName(),QString("Devices"));
        QCOMPARE(session.executions, 0); // Binding never starts a stream.
        QSignalSpy deleted(&peers,&PeerManager::deviceRemovalFinished);
        QVERIFY(QMetaObject::invokeMethod(root.data(),"testRemove"));
        auto removalPopup=root->property("testCurrentPage").value<QObject*>()->findChild<QObject*>("removeDeviceDialog");
        QVERIFY(removalPopup); QTRY_VERIFY(removalPopup->property("visible").toBool());
        QTest::qWait(250);
        auto deleteButton=findVisual(window->contentItem(),"dialogAcceptButton"); QVERIFY(deleteButton);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,deleteButton->mapToScene(QPointF(deleteButton->width()/2,deleteButton->height()/2)).toPoint());
        QTRY_COMPARE_WITH_TIMEOUT(deleted.size(),1,1000);
        QCOMPARE(deleted.first().first().toString(),QString("device-b"));
        QVERIFY(deleted.first().at(1).toBool());
        QVERIFY(QMetaObject::invokeMethod(root.data(),"showOperation",Q_ARG(QVariant,QString("Disconnecting…"))));
        auto progress=root->findChild<QObject*>("operationProgress"); QVERIFY(progress);
        QTRY_VERIFY(progress->property("visible").toBool());
        QVERIFY(window->isVisible());
        QVERIFY(QMetaObject::invokeMethod(root.data(),"finishOperation"));
        QTRY_VERIFY(!progress->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(root.data(),"testStart"));
        QTRY_COMPARE(session.executions,1);
        QCOMPARE(root->property("activeHostId").toString(),QString("device-a"));
        QVERIFY(QMetaObject::invokeMethod(&peers,"peerBound",Q_ARG(QVariantMap,newlyBound),Q_ARG(bool,true)));
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
        QPointer<QObject> retainedDevices;
        int recalls=0;
        connect(&host,&HostManager::viewerRecallRequested,this,[&]{recalls++;});
        for(int i=0;i<50;++i) {
            QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevicesDuringSession"));
            QCOMPARE(root->property("testDepth").toInt(),3);
            auto currentDevices = root->property("testCurrentPage").value<QObject*>();
            if (i == 0) retainedDevices = currentDevices;
            else QCOMPARE(currentDevices, retainedDevices.data());
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
        QVERIFY(QMetaObject::invokeMethod(cardA,"detailsRequested"));
        auto devicePanel = a->findChild<QObject*>("devicePanel-device-a"); QVERIFY(devicePanel);
        QTRY_VERIFY(devicePanel->property("visible").toBool());
        QCOMPARE(root->property("testDepth").toInt(),devicePageDepth);
        if(!qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS").isEmpty()) {
            QTest::qWait(200);
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS")+"/device-panel.png"));
        }
        auto panelAlias = findVisual(window->contentItem(),"panel-setAlias-device-a");
        QVERIFY(panelAlias); QVERIFY(panelAlias->isVisible()); QVERIFY(panelAlias->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(panelAlias,"clicked"));
        auto renameDialog = grid->findChild<QObject*>("renameDeviceDialog"); QVERIFY(renameDialog);
        QTRY_VERIFY(renameDialog->property("visible").toBool());
        QTRY_VERIFY(!devicePanel->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(renameDialog,"reject"));
        QVERIFY(QMetaObject::invokeMethod(cardA,"detailsRequested"));
        QTRY_VERIFY(devicePanel->property("visible").toBool());
        auto panelGear = devicePanel->findChild<QObject*>("panelDeviceSettings"); QVERIFY(panelGear);
        QVERIFY(QMetaObject::invokeMethod(panelGear,"clicked"));
        QTRY_VERIFY(!devicePanel->property("visible").toBool());
        auto deviceSettingsDialog = grid->findChild<QObject*>("deviceSettingsDialog"); QVERIFY(deviceSettingsDialog);
        QTRY_VERIFY(deviceSettingsDialog->property("visible").toBool());
        QCOMPARE(root->property("testDepth").toInt(),devicePageDepth);
        QVERIFY(!deviceSettingsDialog->findChild<QObject*>("changeDeviceAddress"));
        if(!qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS").isEmpty()) {
            QTest::qWait(200);
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS")+"/device-settings-popup.png"));
        }
        auto closeDeviceSettings = deviceSettingsDialog->findChild<QObject*>("panelClose"); QVERIFY(closeDeviceSettings);
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
        // The data saver switch is one icon beside the traffic chip and starts off.
        auto saver=findVisual(window->contentItem(),"dataSaverButton"); QVERIFY(saver);
        auto traffic=findVisual(window->contentItem(),"trafficSummary");
        QVERIFY(saver->property("checkable").toBool());
        QVERIFY(!saver->property("checked").toBool());
        QVERIFY(saver->width() <= 44);
        QVERIFY(traffic->mapToScene(QPointF()).x() < saver->mapToScene(QPointF()).x());
        QVERIFY(saver->mapToScene(QPointF()).x() < arrange->mapToScene(QPointF()).x());
        // Daily budget: the chip turns amber at 70% and red at 85% of the limit.
        auto budgetChip = findVisual(window->contentItem(),"trafficBudgetChip"); QVERIFY(budgetChip);
        QVERIFY(!budgetChip->isVisible());
        budget.setEnabled(true); budget.setLimitGb(1.0);
        QTRY_VERIFY(budgetChip->isVisible());
        const auto normalColor = budgetChip->property("labelColor").value<QColor>();
        budget.account("test-session", 750000000);
        QCOMPARE(budget.level(), 1);
        QTRY_VERIFY(budgetChip->property("text").toString().contains("75%"));
        const auto warningColor = budgetChip->property("labelColor").value<QColor>();
        if(!qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS").isEmpty()) {
            QVERIFY(QMetaObject::invokeMethod(saver,"toggle"));
            QTest::qWait(200);
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS")+"/topbar-budget.png"));
            QVERIFY(QMetaObject::invokeMethod(saver,"toggle"));
        }
        budget.account("test-session", 900000000);
        QCOMPARE(budget.level(), 2);
        const auto criticalColor = budgetChip->property("labelColor").value<QColor>();
        // The budget chip sits between the traffic chip and the data saver switch.
        QTRY_VERIFY(traffic->mapToScene(QPointF()).x() < budgetChip->mapToScene(QPointF()).x());
        QTRY_VERIFY(budgetChip->mapToScene(QPointF()).x() < saver->mapToScene(QPointF()).x());
        QVERIFY(normalColor != warningColor && warningColor != criticalColor && normalColor != criticalColor);
        auto budgetSection = window->findChild<QObject*>("trafficBudgetSection");
        QVERIFY(budgetSection);
        QVERIFY(window->findChild<QObject*>("trafficBudgetReset"));
        QVERIFY(window->findChild<QObject*>("trafficBudgetLimit"));
        budget.reset(); QCOMPARE(budget.usedBytes(), 0.0); QCOMPARE(budget.level(), 0);
        QTRY_COMPARE(budgetChip->property("labelColor").value<QColor>(), normalColor);
        budget.setEnabled(false);
        // Exceeding an enabled budget is reported once per day; a reset re-arms it.
        TrafficBudget detached(nullptr, false);
        detached.today = [] { return QStringLiteral("2026-10-15"); };
        QSignalSpy exceeded(&detached, &TrafficBudget::exceeded);
        detached.setLimitGb(0.5);
        detached.account("s", 600000000); QCOMPARE(exceeded.count(), 0); // disabled
        detached.setEnabled(true); QCOMPARE(exceeded.count(), 1);
        detached.account("s", 700000000); QCOMPARE(exceeded.count(), 1);
        detached.reset(); detached.account("s", 1300000000); QCOMPARE(exceeded.count(), 2);
        detached.today = [] { return QStringLiteral("2026-10-16"); };
        detached.account("s", 1400000000); QCOMPARE(detached.usedBytes(), 100000000.0); QCOMPARE(detached.level(), 0);
        // The right-hand buttons stay inside the window at every width.
        for (int width : {1120, 800, 640}) {
            window->resize(width, 620); QTest::qWait(10);
            QVERIFY(grid->property("resizing").toBool());
            const int gridWidth=int(grid->property("width").toReal());
            const int columns=grid->property("columns").toInt();
            QCOMPARE(grid->property("cellWidth").toInt(),
                     std::min(204, (std::max(1,gridWidth-32)-(columns-1)*12)/columns+12));
            QTRY_VERIFY_WITH_TIMEOUT(!grid->property("resizing").toBool(),250);
            const int cell=grid->property("cellWidth").toInt();
            const int side=grid->property("cardSide").toInt();
            QCOMPARE(grid->property("visualMargin").toInt(),
                     std::max(0, (gridWidth-(columns-1)*cell-side)/2));
            QCOMPARE(grid->property("leftMargin").toReal(),0.0);
            QCOMPARE(grid->property("rightMargin").toReal(),0.0);
            auto settingsButton=findVisual(window->contentItem(),"settingsButton");
            QVERIFY(settingsButton->mapToScene(QPointF(settingsButton->width(),0)).x() <= width);
            auto saverButton=findVisual(window->contentItem(),"dataSaverButton");
            QVERIFY(saverButton->isVisible());
            QVERIFY(saverButton->mapToScene(QPointF(saverButton->width(),0)).x() <= width);
        }
        // Exercise full rows across fractional widths and every column boundary.
        auto resizeModel=qobject_cast<TestComputers*>(grid->property("model").value<QObject*>());
        QVERIFY(resizeModel); resizeModel->setDeviceCount(12);
        for (int width : {940, 941, 1021, 1174, 1175, 1176, 1410, 1409, 939, 705, 704}) {
            window->resize(width,620); QTest::qWait(25);
            const int columns=grid->property("columns").toInt();
            const qreal cell=grid->property("cellWidth").toReal();
            const qreal row=grid->property("cellHeight").toReal();
            const qreal margin=grid->property("visualMargin").toReal();
            for(int i=0; i<columns+1; ++i) {
                auto item=findVisual(gridItem,"device-"+resizeModel->devices[i]); QVERIFY(item);
                QVERIFY2(std::abs(item->x()-(i%columns)*cell)<1.0,"card must occupy its expected column during resize");
                QVERIFY2(std::abs(item->mapToItem(qobject_cast<QQuickItem*>(grid),QPointF(0,0)).x()-margin-(i%columns)*cell)<1.0,
                         "card must stay centered inside the grid viewport");
                QVERIFY2(std::abs(item->y()-(i/columns)*row)<1.0,"card must occupy its expected row during resize");
                QCOMPARE(item->width(), item->height());
                QVERIFY(item->width() <= 192);
                auto surface=qobject_cast<QQuickItem*>(item->property("contentItem").value<QObject*>()); QVERIFY(surface);
                QCOMPARE(surface->width(), surface->height());
                QCOMPARE(surface->width(), item->width());
                QVERIFY(item->isVisible());
            }
            auto firstRow=qobject_cast<QQuickItem*>(findVisual(gridItem,"device-"+resizeModel->devices[0]));
            auto lastRow=qobject_cast<QQuickItem*>(findVisual(gridItem,"device-"+resizeModel->devices[columns-1]));
            QVERIFY(firstRow && lastRow);
            auto viewport=qobject_cast<QQuickItem*>(grid); QVERIFY(viewport);
            const qreal left=firstRow->mapToItem(viewport,QPointF()).x();
            const qreal right=grid->property("width").toReal()-lastRow->mapToItem(viewport,QPointF(lastRow->width(),0)).x();
            QVERIFY2(std::abs(left-right)<=1.0,"visible card row must have equal side margins");
        }
        if(!qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS").isEmpty()) {
            window->resize(1021,800); QTest::qWait(100);
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS")+"/devices-resize.png"));
        }
        resizeModel->setDeviceCount(2);
        resizeModel->setShowAdd(true);
        for (int width : {560, 640, 720, 800}) {
            window->resize(width,620); QTest::qWait(100);
            auto add=findVisual(gridItem,"addCard"); QVERIFY(add);
            auto plus=findVisual(add,"addPlus"); QVERIFY(plus);
            auto addDevice=findVisual(add,"addDevice"); QVERIFY(addDevice);
            auto addGroup=findVisual(add,"addGroup"); QVERIFY(addGroup);
            const auto top=[add](QQuickItem* item) { return item->mapToItem(add,QPointF(0,0)).y(); };
            QVERIFY(plus->height()>0);
            QVERIFY(top(plus)>=0);
            QVERIFY(top(plus)+plus->height()<=top(addDevice)+0.5);
            QVERIFY(top(addDevice)+addDevice->height()<=top(addGroup)+0.5);
            QVERIFY(top(addGroup)+addGroup->height()<=add->height()+0.5);
            if (width==720 && !qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS").isEmpty())
                QVERIFY(window->grabWindow().save(qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS")+"/add-card-narrow.png"));
        }
        resizeModel->setShowAdd(false);
        window->resize(800,620); QTest::qWait(100);
        a=findVisual(gridItem,"device-device-a"); b=findVisual(gridItem,"device-device-b");
        first=qobject_cast<QQuickItem*>(a); QVERIFY(first && b);
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
        auto groupPanel=grid->findChild<QObject*>("groupPanel"); QVERIFY(groupPanel);
        QVERIFY(QMetaObject::invokeMethod(groupPanel,"openFor",Q_ARG(QVariant,groupId)));
        QTRY_VERIFY(groupPanel->property("visible").toBool());
        if(!qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS").isEmpty()) {
            QTest::qWait(200); QVERIFY(window->grabWindow().save(qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS")+"/group-panel.png"));
        }
        auto deleteGroup=groupPanel->findChild<QObject*>("deleteGroup"); QVERIFY(deleteGroup);
        QVERIFY(QMetaObject::invokeMethod(deleteGroup,"clicked"));
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
                // The page name sits right beside the section buttons and follows them.
                auto name=findVisual(window->contentItem(),"pageName"); QVERIFY(name);
                QVERIFY(name->isVisible());
                QCOMPARE(name->property("text").toString(),QString("Manual"));
                const QPointF nameEnd=name->mapToScene(QPointF(name->width(),0));
                const QPointF buttonsStart=qobject_cast<QQuickItem*>(manual)->mapToScene(QPointF(0,0));
                QVERIFY(nameEnd.x() <= buttonsStart.x());
                QVERIFY(buttonsStart.x()-nameEnd.x() < 24);
                QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevicesDuringSession")); QTest::qWait(150);
                QCOMPARE(name->property("text").toString(),QString("Devices"));
            }
            QVERIFY(QMetaObject::invokeMethod(root.data(),"testCards"));
            QTest::qWait(100);
            auto current=root->property("testCurrentPage").value<QObject*>(); QVERIFY(current);
            QVERIFY(!current->property("compact").toBool());
            QVERIFY(window->grabWindow().save(shots+"/cards-dark.png"));
            window->resize(640,620); QTest::qWait(100);
            QVERIFY(!current->property("compact").toBool());
            QTranslator chinese;
            const auto catalogs=qEnvironmentVariable("DESKPORT_TRANSLATIONS_DIR",qEnvironmentVariable("TEST_GUI_DIR")+"/../languages");
            QVERIFY(chinese.load(catalogs+"/qml_zh_CN.qm"));
            QVERIFY(QCoreApplication::installTranslator(&chinese)); engine.retranslate();
            QTest::qWait(100); QVERIFY(window->grabWindow().save(shots+"/devices-chinese-narrow.png"));
            {
                // Device settings open as a panel over Devices, in Chinese.
                const int depth=root->property("testDepth").toInt();
                auto device=findVisual(qobject_cast<QQuickItem*>(current),"device-device-a"); QVERIFY(device);
                auto panel=device->findChild<QObject*>("devicePanel-device-a"); QVERIFY(panel);
                QVERIFY(QMetaObject::invokeMethod(panel,"settingsRequested"));
                auto settingsPanel=current->findChild<QObject*>("deviceSettingsDialog"); QVERIFY(settingsPanel);
                QTRY_VERIFY(settingsPanel->property("visible").toBool());
                QTest::qWait(250); QVERIFY(window->grabWindow().save(shots+"/device-settings-chinese.png"));
                auto advanced=findVisual(window->contentItem(),"deviceAdvancedButton"); QVERIFY(advanced);
                QVERIFY(QMetaObject::invokeMethod(advanced,"clicked"));
                QTest::qWait(250); QVERIFY(window->grabWindow().save(shots+"/device-advanced-chinese.png"));
                QCOMPARE(root->property("testDepth").toInt(),depth);
                QVERIFY(QMetaObject::invokeMethod(settingsPanel,"close")); QTest::qWait(250);
            }

            QCoreApplication::removeTranslator(&chinese); engine.retranslate();
            // Every redesigned surface in both themes, from synthetic data only.
            for (int mode : {1, 2}) {
                const QString suffix = mode == 1 ? "-light" : "-dark";
                auto capture=[&](const QString& name) { QTest::qWait(250); QVERIFY(window->grabWindow().save(shots+"/"+name+suffix+".png")); };
                QVERIFY(QMetaObject::invokeMethod(root.data(),"testTheme",Q_ARG(QVariant,mode)));
                window->resize(1120,760);
                QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevicesDuringSession")); QTest::qWait(200);
                auto page=root->property("testCurrentPage").value<QObject*>(); QVERIFY(page);
                auto shown=page->property("model").value<QObject*>(); QVERIFY(shown);
                QVERIFY(QMetaObject::invokeMethod(shown,"setShowAdd",Q_ARG(bool,true)));
                capture("final-devices");
                QVERIFY(QMetaObject::invokeMethod(shown,"setSecondState",Q_ARG(int,2)));
                capture("final-devices-offline");
                QVERIFY(QMetaObject::invokeMethod(shown,"setSecondState",Q_ARG(int,1)));
                capture("final-devices-checking");
                QVERIFY(QMetaObject::invokeMethod(shown,"setSecondState",Q_ARG(int,2)));
                QTest::qWait(100);
                {
                    auto offline=findVisual(qobject_cast<QQuickItem*>(page),"device-device-b"); QVERIFY(offline);
                    auto card=offline->property("contentItem").value<QObject*>(); QVERIFY(card);
                    QCOMPARE(card->property("actionText").toString(),QString("Troubleshoot"));
                    QVERIFY(QMetaObject::invokeMethod(card,"activateRequested"));
                    auto panel=offline->findChild<QObject*>("devicePanel-device-b"); QVERIFY(panel);
                    QTRY_VERIFY(panel->property("visible").toBool());
                    capture("final-panel-offline");
                    QVERIFY(QMetaObject::invokeMethod(panel,"close")); QTest::qWait(250);
                }
                QVERIFY(QMetaObject::invokeMethod(shown,"setSecondState",Q_ARG(int,0)));
                QVERIFY(QMetaObject::invokeMethod(shown,"setShowAdd",Q_ARG(bool,false))); QTest::qWait(150);
                auto device=findVisual(qobject_cast<QQuickItem*>(page),"device-device-a"); QVERIFY(device);
                auto card=device->property("contentItem").value<QObject*>(); QVERIFY(card);
                QVERIFY(QMetaObject::invokeMethod(card,"detailsRequested"));
                auto panel=device->findChild<QObject*>("devicePanel-device-a"); QVERIFY(panel);
                QTRY_VERIFY(panel->property("visible").toBool());
                capture("final-panel");
                auto gear=panel->findChild<QObject*>("panelDeviceSettings"); QVERIFY(gear);
                QVERIFY(QMetaObject::invokeMethod(gear,"clicked"));
                auto settingsPanel=page->findChild<QObject*>("deviceSettingsDialog"); QVERIFY(settingsPanel);
                QTRY_VERIFY(settingsPanel->property("visible").toBool());
                const QSizeF panelSize(settingsPanel->property("width").toReal(),settingsPanel->property("height").toReal());
                QVERIFY(panelSize.width() <= 460); QVERIFY(panelSize.height() <= 760*0.82+1);
                capture("final-device-settings");
                auto policy=findVisual(window->contentItem(),"deviceDisplayPolicy"); QVERIFY(policy);
                QCOMPARE(policy->property("value").toString(),QString("Main + mirror"));
                QVERIFY(QMetaObject::invokeMethod(policy,"clicked"));
                auto options=policy->findChild<QObject*>("deviceDisplayPolicyPanel"); QVERIFY(options);
                QTRY_VERIFY(options->property("visible").toBool());
                capture("final-option-panel");
                QVERIFY(QMetaObject::invokeMethod(options,"close")); QTest::qWait(250);
                auto advanced=findVisual(window->contentItem(),"deviceAdvancedButton"); QVERIFY(advanced);
                QVERIFY(QMetaObject::invokeMethod(advanced,"clicked"));
                capture("final-device-advanced");
                QCOMPARE(root->property("testDepth").toInt(),3);
                QVERIFY(QMetaObject::invokeMethod(settingsPanel,"close")); QTest::qWait(250);
                auto remove=page->findChild<QObject*>("removeDeviceDialog"); QVERIFY(remove);
                remove->setProperty("pcName","Studio");
                QVERIFY(QMetaObject::invokeMethod(remove,"open"));
                capture("final-remove");
                QVERIFY(QMetaObject::invokeMethod(remove,"close")); QTest::qWait(250);
                // Pages below replace this device list; do not use it afterwards.
                auto manualButton=findVisual(window->contentItem(),"manualButton"); QVERIFY(manualButton);
                QVERIFY(QMetaObject::invokeMethod(manualButton,"clicked"));
                capture("final-manual");
                {
                    auto manualPage=qobject_cast<QQuickItem*>(root->property("testCurrentPage").value<QObject*>());
                    auto second=findVisual(manualPage,"chapterHeader1"); QVERIFY(second);
                    QVERIFY(QMetaObject::invokeMethod(second,"clicked"));
                    QCOMPARE(manualPage->property("openChapter").toInt(),1);
                    capture("final-manual-second");
                    QVERIFY(QMetaObject::invokeMethod(second,"clicked"));
                    QCOMPARE(manualPage->property("openChapter").toInt(),-1);
                    QVERIFY(QMetaObject::invokeMethod(findVisual(manualPage,"chapterHeader0"),"clicked"));
                }
                auto settingsButton=findVisual(window->contentItem(),"settingsButton"); QVERIFY(settingsButton);
                QVERIFY(QMetaObject::invokeMethod(settingsButton,"clicked"));
                capture("final-settings");
                auto settingsPage=root->property("testCurrentPage").value<QObject*>(); QVERIFY(settingsPage);
                auto privacy=settingsPage->findChild<QObject*>("privacyPanel"); QVERIFY(privacy);
                QVERIFY(QMetaObject::invokeMethod(privacy,"open"));
                capture("final-privacy");
                QVERIFY(QMetaObject::invokeMethod(privacy,"close")); QTest::qWait(250);
            }
            QVERIFY(QMetaObject::invokeMethod(root.data(),"showDevicesDuringSession")); QTest::qWait(250);
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
    void shortcutSettingSharesOneRowWithIndependentControls() {
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
        auto item=qobject_cast<QQuickItem*>(page.data()); QVERIFY(item);
        QQuickWindow window; window.resize(660,900);
        item->setParentItem(window.contentItem()); item->setSize(QSizeF(660,900));
        window.show(); QTest::qWait(100);
        auto row=page->findChild<QQuickItem*>("globalShortcutSwitch"); QVERIFY(row);
        auto choice=page->findChild<QQuickItem*>("globalShortcutSwitchValue"); QVERIFY(choice);
        auto dialog=page->findChild<QObject*>("shortcutDialog"); QVERIFY(dialog);
        auto prefs=page->property("preferences").value<QObject*>(); QVERIFY(prefs);
        const bool originallyEnabled=prefs->property("globalShortcutEnabled").toBool();
        QVERIFY(choice->isVisible());
        QVERIFY(row->property("detail").toString().isEmpty());
        QVERIFY(!page->findChild<QObject*>("globalShortcutChoice"));
        QVERIFY(row->isAncestorOf(choice));
        QCOMPARE(choice->property("text").toString(), row->property("value").toString());
        QVERIFY(std::abs(choice->mapToItem(row,QPointF(0,choice->height()/2)).y()-row->height()/2)<1.0);
        QVERIFY(row->height()<=60);
        const auto originalTitle=row->property("title").toString();
        row->setProperty("title",QStringLiteral("A deliberately long translated global window shortcut title"));
        window.resize(480,900); item->setSize(QSizeF(480,900)); QTest::qWait(50);
        QVERIFY(row->height()<=60);
        QVERIFY(choice->mapToItem(row,QPointF(choice->width(),0)).x()<=row->width());
        row->setProperty("title",originalTitle);
        window.resize(660,900); item->setSize(QSizeF(660,900)); QTest::qWait(50);

        // Editing the value must not also toggle its parent switch row.
        QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,choice->mapToScene(QPointF(choice->width()/2,choice->height()/2)).toPoint());
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(prefs->property("globalShortcutEnabled").toBool(),originallyEnabled);
        QVERIFY(QMetaObject::invokeMethod(dialog,"reject"));
        QTRY_VERIFY(!dialog->property("visible").toBool()); QTest::qWait(200);
        QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,row->mapToScene(QPointF(16,row->height()/2)).toPoint());
        QTRY_COMPARE(prefs->property("globalShortcutEnabled").toBool(),!originallyEnabled);
        QVERIFY(!dialog->property("visible").toBool());

        // The same value button stays available while the shortcut is disabled.
        if (prefs->property("globalShortcutEnabled").toBool()) prefs->setProperty("globalShortcutEnabled",false);
        QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,choice->mapToScene(QPointF(choice->width()/2,choice->height()/2)).toPoint());
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(!prefs->property("globalShortcutEnabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog,"reject"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        prefs->setProperty("globalShortcutEnabled",originallyEnabled);
        const auto screenshots=qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            QTest::mouseMove(&window,QPoint(window.width()-4,window.height()-4));
            QDir().mkpath(screenshots); QTest::qWait(150);
            QVERIFY(window.grabWindow().save(screenshots+"/shortcut-setting-inline.png"));
        }
    }
    void customShortcutDialogRecordsCancelsAndResets() {
        TestGlobalShortcut shortcut;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("globalShortcut",&shortcut);
        const auto gui = qEnvironmentVariable("TEST_GUI_DIR");
        QQmlComponent component(&engine);
        component.setData(R"(import QtQuick 2.12
import StreamingPreferences 1.0
import "."
Item {
    property var preferences: StreamingPreferences
    ShortcutDialog { id: dialog; parent: root; objectName: "shortcutDialog" }
    id: root; width: 600; height: 650
})", QUrl::fromLocalFile(gui + "/shortcut-test.qml"));
        QScopedPointer<QObject> root(component.create()); QVERIFY2(root, qPrintable(component.errorString()));
        auto prefs = root->property("preferences").value<QObject*>(); QVERIFY(prefs);
        auto dialog = root->findChild<QObject*>("shortcutDialog"); QVERIFY(dialog);
        QQuickWindow window; window.resize(600,650);
        qobject_cast<QQuickItem*>(root.data())->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QMetaObject::invokeMethod(dialog,"open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(shortcut.recording);
        QCOMPARE(shortcut.recordingWindow.data(),static_cast<QWindow*>(&window));
        QTest::keyClick(&window, Qt::Key_K, Qt::AltModifier | Qt::ControlModifier);
        const auto expected = ShortcutBinding::fromEvent(Qt::Key_K, Qt::AltModifier | Qt::ControlModifier);
        QCOMPARE(dialog->property("pending").toString(), expected);
        const auto screenshots = qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            QDir().mkpath(screenshots);
            QTest::qWait(100);
            QVERIFY(window.grabWindow().save(screenshots + "/custom-shortcut.png"));
        }
        QVERIFY(QMetaObject::invokeMethod(dialog,"reject"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QTRY_VERIFY(!shortcut.recording);
        QVERIFY(shortcut.recordingWindow.isNull());
        QCOMPARE(prefs->property("globalShortcutSequence").toString(), QStringLiteral("Meta+'"));
        QVERIFY(QMetaObject::invokeMethod(dialog,"open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::keyClick(&window, Qt::Key_A);
        QVERIFY(dialog->property("invalid").toBool());
        auto apply = dialog->findChild<QObject*>("shortcutApply"); QVERIFY(apply);
        QVERIFY(!apply->property("enabled").toBool());
        QTest::keyClick(&window, Qt::Key_K, Qt::AltModifier | Qt::ControlModifier);
        QVERIFY(QMetaObject::invokeMethod(dialog,"apply"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QTRY_VERIFY(!shortcut.recording);
        QVERIFY(shortcut.recordingWindow.isNull());
        QCOMPARE(prefs->property("globalShortcutSequence").toString(), expected);
        // The chooser uses ordinary controls even when a compositor consumes
        // the complete Meta+digit chord before Qt can receive any key event.
        QVERIFY(QMetaObject::invokeMethod(dialog,"open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto chooser = dialog->findChild<QQuickItem*>("shortcutChooseKeys"); QVERIFY(chooser);
        const auto click = [&window](QQuickItem* item) {
            QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,
                             item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());
        };
        click(chooser);
        QVERIFY(dialog->property("choosing").toBool());
        QVERIFY(shortcut.recording);
        QVERIFY(shortcut.recordingWindow.isNull());
        QTest::qWait(50);
        click(chooser);
        QVERIFY(!dialog->property("choosing").toBool());
        QVERIFY(shortcut.recording);
        QCOMPARE(shortcut.recordingWindow.data(),static_cast<QWindow*>(&window));
        QTest::qWait(50);
        click(chooser);
        QVERIFY(dialog->property("choosing").toBool());
        QVERIFY(shortcut.recordingWindow.isNull());
        QTest::qWait(50);
        QList<QQuickItem*> modifiers;
        for (int i = 0; i < 4; ++i) {
            auto modifier = findVisual(window.contentItem(),QString("shortcutModifier%1").arg(i));
            QVERIFY(modifier); modifiers << modifier;
            if (modifier->property("checked").toBool() != (i == 2)) click(modifier);
        }
        QCOMPARE(modifiers[0]->property("text").toString(),QString("Control"));
#ifdef Q_OS_MACOS
        QCOMPARE(modifiers[2]->property("text").toString(),QString("Command"));
#else
        QCOMPARE(modifiers[2]->property("text").toString(),QString("Meta"));
#endif
        auto primary = dialog->findChild<QQuickItem*>("shortcutPrimaryKey"); QVERIFY(primary);
        const auto choices = dialog->property("keyChoices").toList();
        int digitIndex = -1;
        for (int i = 0; i < choices.size(); ++i)
            if (choices[i].toMap()["key"].toInt() == Qt::Key_1) digitIndex = i;
        QVERIFY(digitIndex >= 0);
        click(primary);
        auto popup = primary->property("popup").value<QObject*>(); QVERIFY(popup);
        QTRY_VERIFY(popup->property("visible").toBool());
        QTest::keyClick(&window,Qt::Key_Home);
        for (int i = 0; i < digitIndex; ++i) QTest::keyClick(&window,Qt::Key_Down);
        QTest::keyClick(&window,Qt::Key_Return);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QCOMPARE(primary->property("currentIndex").toInt(),digitIndex);
        QCOMPARE(dialog->property("pending").toString(), QString("Meta+1"));
        QVERIFY(!dialog->property("invalid").toBool());
        // The physical Control label must remain distinct from Command on Mac.
        click(modifiers[2]); click(modifiers[0]);
        QCOMPARE(dialog->property("pending").toString(),QString("Ctrl+1"));
        click(modifiers[0]); click(modifiers[2]);
        QCOMPARE(dialog->property("pending").toString(),QString("Meta+1"));
        if (!screenshots.isEmpty()) {
            QTest::mouseMove(&window,QPoint(window.width()-4,window.height()-4));
            QTest::qWait(100);
            QVERIFY(window.grabWindow().save(screenshots + "/custom-shortcut-choose-keys.png"));
        }
        QVERIFY(QMetaObject::invokeMethod(dialog,"apply"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QTRY_VERIFY(!shortcut.recording);
        QVERIFY(shortcut.recordingWindow.isNull());
        QCOMPARE(prefs->property("globalShortcutSequence").toString(), QString("Meta+1"));
        QVERIFY(QMetaObject::invokeMethod(dialog,"open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        click(chooser);
        for (int i = 0; i < modifiers.size(); ++i)
            if (modifiers[i]->property("checked").toBool() != (i == 3)) click(modifiers[i]);
        QVERIFY(dialog->property("invalid").toBool());
        QVERIFY(!apply->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog,"reject"));
        QCOMPARE(prefs->property("globalShortcutSequence").toString(), QString("Meta+1"));
        QVERIFY(QMetaObject::invokeMethod(dialog,"open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto reset = dialog->findChild<QObject*>("shortcutReset"); QVERIFY(reset);
        QVERIFY(QMetaObject::invokeMethod(reset,"clicked"));
        QCOMPARE(dialog->property("pending").toString(), QStringLiteral("Meta+'"));
        QVERIFY(QMetaObject::invokeMethod(dialog,"apply"));
        QCOMPARE(prefs->property("globalShortcutSequence").toString(), QStringLiteral("Meta+'"));
    }
    void shortcutChooserSupportsKeyboardAndSmallWindows() {
        QQmlEngine engine;
        const auto gui=qEnvironmentVariable("TEST_GUI_DIR");
        QQmlComponent component(&engine);
        component.setData(R"(import QtQuick 2.12
import StreamingPreferences 1.0
import "."
Item {
    property var preferences: StreamingPreferences
    ShortcutDialog { parent: root; objectName: "shortcutDialog" }
    id: root; width: 480; height: 360
})",QUrl::fromLocalFile(gui+"/shortcut-keyboard-test.qml"));
        QScopedPointer<QObject> root(component.create()); QVERIFY2(root,qPrintable(component.errorString()));
        auto prefs=root->property("preferences").value<QObject*>(); QVERIFY(prefs);
        const auto originalSequence=prefs->property("globalShortcutSequence").toString();
        auto dialog=root->findChild<QObject*>("shortcutDialog"); QVERIFY(dialog);
        QQuickWindow window; window.resize(480,360);
        qobject_cast<QQuickItem*>(root.data())->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QMetaObject::invokeMethod(dialog,"open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto chooser=dialog->findChild<QQuickItem*>("shortcutChooseKeys"); QVERIFY(chooser);
        auto primary=dialog->findChild<QQuickItem*>("shortcutPrimaryKey"); QVERIFY(primary);
        auto apply=dialog->findChild<QQuickItem*>("shortcutApply"); QVERIFY(apply);
        auto scroll=dialog->findChild<QQuickItem*>("shortcutScroll"); QVERIFY(scroll);
        const auto beforeNavigation=dialog->property("pending").toString();
        for (int i=0; i<12 && !chooser->hasActiveFocus(); ++i) QTest::keyClick(&window,Qt::Key_Tab);
        QVERIFY(chooser->hasActiveFocus());
        QCOMPARE(dialog->property("pending").toString(),beforeNavigation);
        QTest::keyClick(&window,Qt::Key_Space);
        QVERIFY(dialog->property("choosing").toBool());
        for (int i=0; i<12 && !primary->hasActiveFocus(); ++i) QTest::keyClick(&window,Qt::Key_Tab);
        QVERIFY(primary->hasActiveFocus());
        QTest::keyClick(&window,Qt::Key_Home);
        QTest::keyClick(&window,Qt::Key_Down);
        QCOMPARE(dialog->property("chosenKey").toInt(),int(Qt::Key_B));
        QTest::qWait(100);
        QVERIFY(dialog->property("height").toReal()<=360*0.82+1);
        const auto keyTop=primary->mapToScene(QPointF()).y();
        const auto keyBottom=primary->mapToScene(QPointF(0,primary->height())).y();
        const auto scrollTop=scroll->mapToScene(QPointF()).y();
        const auto scrollBottom=scroll->mapToScene(QPointF(0,scroll->height())).y();
        QVERIFY(keyTop>=scrollTop && keyBottom<=scrollBottom+1);
        const auto screenshots=qEnvironmentVariable("DESKPORT_UI_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            QDir().mkpath(screenshots);
            QVERIFY(window.grabWindow().save(screenshots+"/custom-shortcut-small-window.png"));
        }
        for (int i=0; i<12 && !apply->hasActiveFocus(); ++i) QTest::keyClick(&window,Qt::Key_Tab);
        QVERIFY(apply->hasActiveFocus());
        const auto expected=dialog->property("pending").toString();
        QTest::keyClick(&window,Qt::Key_Space);
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(prefs->property("globalShortcutSequence").toString(),expected);
        prefs->setProperty("globalShortcutSequence",originalSequence);
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
        const QString englishDragHint=page->property("windowDragHint").toString();
        const QString macWindowDragHint=QStringLiteral("Hold Control+Command: drag inside the picture to move the window, or drag its edges or corners to resize it.");
        QCOMPARE(english,QString("Make DeskPort your own."));
#ifdef Q_OS_MACOS
        QCOMPARE(englishDragHint,macWindowDragHint);
#elif defined(Q_OS_WIN)
        QCOMPARE(englishDragHint,QString("To move or resize the window, turn this option off."));
#else
        QCOMPARE(englishDragHint,QString("Move the window with your desktop shortcut, such as Meta+drag on KDE Plasma."));
#endif
        auto themeChoice=page->findChild<QObject*>("themeChoice"); QVERIFY(themeChoice);
        auto shortcut=page->findChild<QObject*>("globalShortcutSwitch"); QVERIFY(shortcut);
        QVERIFY(shortcut->property("on").toBool());
        QVERIFY(QMetaObject::invokeMethod(shortcut,"switched",Q_ARG(bool,false)));
        QVERIFY(!shortcut->property("on").toBool());
        QVERIFY(QMetaObject::invokeMethod(shortcut,"switched",Q_ARG(bool,true)));
        QVERIFY(shortcut->property("on").toBool());
        QVERIFY(!page->findChild<QObject*>("windowModeChoice"));
        for (const auto& language : {"zh_CN","zh_TW","ja","ko","de","fr","es","pt","ar","bo","it","ru","id","tr","vi","th","nl","pl","uk","hi","ms"}) {
            QTranslator translator;
            const auto catalogs = qEnvironmentVariable("DESKPORT_TRANSLATIONS_DIR", gui+"/../languages");
            QVERIFY(translator.load(catalogs+"/qml_"+language+".qm"));
            QVERIFY(QCoreApplication::installTranslator(&translator));
            engine.retranslate();
            QVERIFY(page->property("heading").toString()!=english);
            QVERIFY(page->property("windowDragHint").toString()!=englishDragHint);
            const auto localizedMacDragHint=translator.translate("SettingsHome",macWindowDragHint.toUtf8().constData());
            QVERIFY(!localizedMacDragHint.isEmpty());
            QVERIFY(localizedMacDragHint!=macWindowDragHint);
            QCOMPARE(themeChoice->property("currentIndex").toInt(),0);
            QVERIFY(!translator.translate("SettingsHome","Follow system").isEmpty());
            QVERIFY(!translator.translate("SettingsHome","Enable diagnostic logs").isEmpty());
            QVERIFY(!translator.translate("SettingsHome","Global window shortcut").isEmpty());
            QVERIFY(!translator.translate("SettingsHome","Change shortcut").isEmpty());
            QVERIFY(!translator.translate("ShortcutDialog","Save shortcut").isEmpty());
            QVERIFY(!translator.translate("SettingsHome","Match the client window resolution").isEmpty());
            QVERIFY(!translator.translate("HostView","Built-in virtual display").isEmpty());
            QVERIFY(!translator.translate("HostView","Active · %1 × %2 pixels").isEmpty());
            QVERIFY(!translator.translate("BindingApproval","Allow & bind").isEmpty());
            QCoreApplication::removeTranslator(&translator);
            engine.retranslate();
            QCOMPARE(page->property("heading").toString(),english);
        }
    }
    void bundledManualIncludesDesktopFeaturesInChineseAndEnglish() {
        Manual manual;
        for (const auto& language : {"en", "zh-Hans", "zh-Hant"}) {
            const auto chapters = manual.chapters(language);
            const auto shortcut = std::find_if(chapters.begin(), chapters.end(), [](const QVariant& entry) {
                return entry.toMap()["id"] == "shortcuts";
            });
            QVERIFY(shortcut != chapters.end());
            const auto steps = shortcut->toMap()["steps"].toStringList().join('\n');
            QVERIFY(steps.contains("Meta + '"));
            QVERIFY(steps.contains("Ctrl+Alt+Shift+Z"));
            QVERIFY(steps.contains(language == QByteArray("en") ? "on by default" :
                                   language == QByteArray("zh-Hans") ? "默认开启" : "預設開啟"));
            QStringList ids;
            for (const auto& entry : chapters) ids << entry.toMap()["id"].toString();
            for (const auto& id : {"windows", "input", "quality", "browser", "diagnostics", "trouble"})
                QVERIFY(ids.contains(id));
        }
    }
};
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir settings;
    if (!settings.isValid()) return 1;
    QCoreApplication::setOrganizationName("DeskPortTests");
    QCoreApplication::setApplicationName("UiPages");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    UiPages tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "ui-pages.moc"
