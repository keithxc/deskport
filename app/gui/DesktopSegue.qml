import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import AppModel 1.0
import ComputerManager 1.0

Item {
    id: page
    property int computerIndex
    property bool initialized: false
    property bool finished: false
    property string errorText: ""

    AppModel { id: apps }

    function fail(message) {
        if (finished) return
        finished = true
        errorText = message
        retry.stop()
        deadline.stop()
    }

    function tryConnect() {
        if (!initialized || finished) return
        var target = apps.desktopTarget()
        if (target.state === "waiting") return
        if (target.state === "missing") {
            fail(qsTr("This device has no Desktop entry. Open Applications from the device menu to choose an app."))
            return
        }
        if (target.state === "busy") {
            fail(qsTr("Another application is running on this device. Open Applications from the device menu to resume it or switch to Desktop."))
            return
        }
        var session = apps.createSessionForApp(target.index)
        if (!session) {
            fail(qsTr("Unable to prepare the desktop connection. Try again."))
            return
        }
        finished = true
        retry.stop()
        deadline.stop()
        // Replace the loading page so disconnect returns directly to Devices.
        stackView.replace(page, Qt.resolvedUrl("StreamSegue.qml"), {
            "session": session, "appName": target.name, "isResume": target.resume
        }, StackView.Immediate)
    }

    StackView.onActivated: {
        if (initialized) return
        apps.initialize(ComputerManager, computerIndex, true)
        initialized = true
        retry.start()
        deadline.start()
        Qt.callLater(page.tryConnect)
    }

    StackView.onDeactivating: { finished = true; retry.stop(); deadline.stop() }

    Connections {
        target: apps
        function onComputerLost() { page.fail(qsTr("The device disconnected. Return to Devices and try again.")) }
    }
    Timer { id: retry; interval: 200; repeat: true; onTriggered: page.tryConnect() }
    Timer { id: deadline; interval: 20000; onTriggered: page.fail(qsTr("Loading the desktop timed out. Return to Devices and try again.")) }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 520)
        spacing: 16
        BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: !page.finished; visible: running }
        Label {
            Layout.fillWidth: true
            text: page.errorText.length ? page.errorText : qsTr("Connecting to desktop…")
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Back to devices")
            onClicked: stackView.pop()
        }
    }
}
