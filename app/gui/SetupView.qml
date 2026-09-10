import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

UiPage {
    id: page
    objectName: qsTr("Getting started")
    heading: qsTr("Make yourself at home.")
    description: qsTr("Connect to another computer right away. To share this one, review its permissions below.")
    function stateText(state) {
        if (state === "allowed") return qsTr("Allowed")
        if (state === "onShare") return qsTr("Checked when sharing")
        if (state === "notRequested") return qsTr("Not requested")
        if (state === "restricted") return qsTr("Restricted by system")
        return qsTr("Needs attention")
    }
    Component.onCompleted: hostManager.refreshPermissions()
    Timer { interval: 3000; repeat: true; running: page.visible; onTriggered: hostManager.refreshPermissions() }
    UiCard {
        ColumnLayout {
            anchors.fill: parent; spacing: 10
            Label { text: hostManager.deviceName; textFormat: Text.PlainText; font.pixelSize: 21; font.weight: Font.DemiBold; color: ui.text }
            Label { text: qsTr("1. Review permissions    2. Add a device    3. Confirm and connect"); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
    Repeater {
        model: hostManager.permissions
        UiCard {
            ColumnLayout {
                anchors.fill: parent; spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: modelData.title; font.pixelSize: 17; font.weight: Font.DemiBold; color: ui.text; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Label { text: page.stateText(modelData.state); color: modelData.state === "allowed" ? ui.accent : ui.warning; font.pixelSize: 12 }
                }
                Label { text: modelData.purpose; color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Button { visible: Qt.platform.os === "osx"; text: qsTr("Open system settings"); onClicked: hostManager.permission(modelData.key) }
                Label { visible: Qt.platform.os === "linux" && modelData.state === "needsSetup"; text: qsTr("Your Linux configuration must allow your user to access /dev/uinput. Connecting to other computers does not need this permission."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
        }
    }
    UiCard {
        visible: Qt.platform.os === "osx"
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Authorize the installed DeskPort app"); font.pixelSize: 17; font.weight: Font.DemiBold; color: ui.text }
            Label { text: qsTr("If a settings pane accepts adding an app, drag the tile below into its list, then turn the switch on. Otherwise use Reveal in Finder. Some panes list DeskPort only after sharing has requested access."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Rectangle {
                id: appTile; Layout.fillWidth: true; height: 66; radius: 10; color: ui.raised; border.color: ui.line
                Row { anchors.centerIn: parent; spacing: 12
                    Image { source: "qrc:/res/deskport.svg"; width: 32; height: 32 }
                    Label { text: qsTr("DeskPort.app  ·  Drag to Settings"); color: ui.text; height: 32; verticalAlignment: Text.AlignVCenter }
                }
                Item {
                    id: dragProxy
                    Drag.active: dragArea.drag.active
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction
                    Drag.mimeData: { "text/uri-list": hostManager.applicationUrl.toString() }
                    Drag.onDragFinished: { x = 0; y = 0 }
                }
                MouseArea { id: dragArea; anchors.fill: parent; drag.target: dragProxy; cursorShape: Qt.OpenHandCursor }
            }
            Button { text: qsTr("Reveal in Finder"); onClicked: hostManager.revealApplication() }
            Label { text: qsTr("After changing permissions, restart sharing. If macOS asks you to reopen DeskPort, follow that prompt. Permission checks do not replace a real picture and input test."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Button { text: qsTr("Check again"); onClicked: hostManager.refreshPermissions() }
        Item { Layout.fillWidth: true }
        Button { text: qsTr("Continue to devices"); highlighted: true; onClicked: { hostManager.completeSetup(); showDevices() } }
    }
}
