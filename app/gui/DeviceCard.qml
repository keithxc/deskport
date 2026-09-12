import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
Rectangle {
    id: card
    property string deviceName: ""
    property string address: ""
    property bool online: false
    property bool paired: false
    property bool unknown: false
    property bool selected: false
    property bool compact: false
    property bool favorite: false
    property bool activeSession: false
    property bool anotherSession: false
    signal moreRequested()
    signal activateRequested()
    readonly property string actionText: activeSession ? qsTr("Return to desktop") : anotherSession ? qsTr("View details") : unknown ? qsTr("Checking…") : !online ? qsTr("Troubleshoot") : paired ? qsTr("Connect") : qsTr("Set up access")
    radius: ui.radius
    color: selected ? ui.raised : ui.surface
    border.color: selected || activeSession ? ui.accent : ui.line
    border.width: selected ? 2 : 1
    GridLayout {
        anchors.fill: parent; anchors.margins: card.compact ? 12 : 16
        columns: card.compact ? 3 : 2
        columnSpacing: 12; rowSpacing: 8
        ColumnLayout {
            Layout.fillWidth: true; Layout.columnSpan: card.compact ? 1 : 2
            spacing: 4
            Label { text: (card.favorite ? "★  " : "") + card.deviceName; textFormat: Text.PlainText; font.pixelSize: ui.title; font.weight: Font.DemiBold; color: ui.text; elide: Text.ElideRight; Layout.fillWidth: true }
            Label {
                text: card.activeSession ? qsTr("Connected") : card.unknown ? qsTr("Checking availability") : card.online ? qsTr("Online") : qsTr("Offline")
                color: card.online || card.activeSession ? ui.accent : ui.muted; font.pixelSize: ui.small
                Layout.fillWidth: true; elide: Text.ElideRight
            }
        }
        UiButton {
            text: card.actionText; highlighted: card.activeSession || (card.online && card.paired && !card.anotherSession)
            enabled: !card.unknown || card.activeSession || card.anotherSession
            Layout.fillWidth: !card.compact
            onClicked: card.activateRequested()
            Accessible.name: text + " · " + card.deviceName
        }
        ToolButton {
            text: "⋯"; implicitWidth: 36
            Accessible.name: qsTr("Device actions") + " · " + card.deviceName
            onClicked: card.moreRequested()
        }
    }
}
