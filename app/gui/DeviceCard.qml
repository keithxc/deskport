import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
Rectangle {
    property string deviceName
    property string address
    property bool online: false
    property bool paired: false
    property bool unknown: false
    property bool selected: false
    signal moreRequested()
    radius: 14
    color: selected ? ui.raised : ui.surface
    border.color: selected ? ui.accent : ui.line
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 20; spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Rectangle {
                width: 38; height: 32; radius: 6; color: ui.raised
                Rectangle { anchors.centerIn: parent; width: 22; height: 15; radius: 2; color: "transparent"; border.color: ui.accent; border.width: 1.5 }
            }
            Item { Layout.fillWidth: true }
            Label { text: unknown ? qsTr("Checking") : online ? qsTr("Online") : qsTr("Offline"); color: online ? ui.accent : ui.muted; font.pixelSize: 12 }
            ToolButton { text: "⋯"; Accessible.name: qsTr("Device actions"); onClicked: moreRequested() }
        }
        Label { text: deviceName; textFormat: Text.PlainText; font.pixelSize: 21; font.weight: Font.DemiBold; color: ui.text; elide: Text.ElideRight; Layout.fillWidth: true }
        Label { text: address.length ? address : qsTr("Looking for an address"); textFormat: Text.PlainText; color: ui.muted; elide: Text.ElideMiddle; Layout.fillWidth: true; font.pixelSize: 12 }
        Rectangle { height: 1; color: ui.line; Layout.fillWidth: true }
        Label { text: unknown ? qsTr("Checking connection…") : !online ? qsTr("Open DeskPort on this device") : paired ? qsTr("Connect to desktop  →") : qsTr("Set up access  →"); color: online ? ui.accent : ui.muted; font.pixelSize: 13 }
    }
}
