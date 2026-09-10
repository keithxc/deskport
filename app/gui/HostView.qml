import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

UiPage {
    objectName: qsTr("Sharing")
    heading: qsTr("Share this computer.")
    description: Qt.platform.os === "osx" ? qsTr("Share a dedicated virtual display. DeskPort stays available when its window is hidden.") : qsTr("Share your current desktop. DeskPort stays available when its window is hidden.")
    UiCard {
        ColumnLayout {
            anchors.fill: parent; spacing: 14
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: hostManager.deviceName; textFormat: Text.PlainText; font.pixelSize: 23; color: ui.text; font.weight: Font.DemiBold }
                    Label { text: hostManager.changing ? qsTr("Updating sharing…") : hostManager.running ? qsTr("Sharing is on") : qsTr("Sharing is off"); color: hostManager.running ? ui.accent : ui.muted }
                }
                UiButton {
                    text: hostManager.running ? qsTr("Stop sharing") : qsTr("Start sharing")
                    highlighted: !hostManager.running
                    enabled: hostManager.available && !hostManager.changing
                    onClicked: hostManager.running ? hostManager.stop() : hostManager.start([2560,2880,3840][size.currentIndex], [1440,1800,2160][size.currentIndex])
                }
            }
            Label { text: hostManager.status; color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
    UiCard {
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Access & permissions"); color: ui.text; font.pixelSize: 18; font.weight: Font.DemiBold }
            Label { text: qsTr("Only devices you approve can connect. A saved binding does not grant system recording or input permissions."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                UiButton { text: qsTr("Review permissions"); onClicked: navigateTo("qrc:/gui/SetupView.qml", "SetupView") }
                UiButton { text: qsTr("Manage devices"); onClicked: navigateTo("qrc:/gui/BindView.qml", "BindView") }
            }
        }
    }
    UiCard {
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Sharing preferences"); color: ui.text; font.pixelSize: 18; font.weight: Font.DemiBold }
            Label { visible: Qt.platform.os === "osx"; text: qsTr("Virtual display size"); color: ui.muted }
            ComboBox { id: size; currentIndex: Math.max(0, [2560,2880,3840].indexOf(hostManager.sharingWidth)); visible: Qt.platform.os === "osx"; model: ["2560 × 1440", "2880 × 1800", "3840 × 2160"]; enabled: !hostManager.running && !hostManager.changing; Layout.preferredWidth: 250 }
            Label { visible: Qt.platform.os === "osx"; text: qsTr("Choose before starting sharing. Changing size during a session is not available yet."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Switch { text: qsTr("Start sharing when I log in"); enabled: hostManager.available; checked: hostManager.loginStart; onClicked: hostManager.setLoginStart(checked) }
        }
    }
    UiCard {
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            UiButton { id: advanced; text: checked ? qsTr("Hide compatibility & diagnostics") : qsTr("Compatibility & diagnostics"); checkable: true }
            ColumnLayout {
                visible: advanced.checked; Layout.fillWidth: true; spacing: 12
                Label { text: qsTr("Legacy PIN pairing"); color: ui.text; font.pixelSize: 17 }
                Label { text: qsTr("For Moonlight or other clients without mutual binding. Enter the PIN shown on that client. This grants access in one direction."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                RowLayout {
                    TextField { id: peer; placeholderText: qsTr("Device name"); maximumLength: 64; Layout.fillWidth: true }
                    TextField { id: pin; placeholderText: qsTr("PIN"); maximumLength: 4; inputMethodHints: Qt.ImhDigitsOnly; Layout.preferredWidth: 100 }
                    UiButton { text: qsTr("Allow"); enabled: hostManager.canPair && /^\d{4}$/.test(pin.text) && peer.text.trim().length > 0; onClicked: { hostManager.pair(pin.text, peer.text); pin.clear() } }
                }
                Label { text: qsTr("Host port: %1  ·  Binding port: %2").arg(hostManager.basePort).arg(peerManager.port); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                UiButton { text: qsTr("Open host logs"); onClicked: hostManager.openLogs() }
            }
        }
    }
}
