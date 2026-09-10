import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

ScrollView {
    id: page
    clip: true
    ColumnLayout {
        width: page.availableWidth
        spacing: 18
        Label { text: qsTr("Share this computer"); font.pixelSize: 28; Layout.margins: 20 }
        Label {
            text: qsTr("DeskPort includes the streaming host and its own virtual display. Sharing continues while this window is hidden; use the tray menu to return.")
            wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
        }
        Label { text: hostManager.status; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.margins: 20 }
        RowLayout {
            Layout.leftMargin: 20
            ComboBox { id: size; model: ["2560×1440 (1280×720 HiDPI)", "2880×1800 (1440×900 HiDPI)", "3840×2160 (1920×1080 HiDPI)"]; enabled: !hostManager.running }
            Button {
                text: qsTr("Start sharing"); enabled: hostManager.available && !hostManager.running
                onClicked: hostManager.start([2560,2880,3840][size.currentIndex], [1440,1800,2160][size.currentIndex])
            }
            Button { text: qsTr("Stop sharing"); enabled: hostManager.running; onClicked: hostManager.stop() }
        }
        CheckBox {
            Layout.leftMargin: 20
            text: qsTr("Start sharing when I log in to this Mac")
            enabled: hostManager.available
            checked: hostManager.loginStart
            onClicked: hostManager.setLoginStart(checked)
        }
        Label { text: qsTr("First-time macOS permissions"); font.pixelSize: 20; Layout.leftMargin: 20 }
        RowLayout {
            Layout.leftMargin: 20
            Button { text: qsTr("Screen recording"); onClicked: hostManager.permission("screen") }
            Button { text: qsTr("Keyboard & mouse"); onClicked: hostManager.permission("input") }
            Button { text: qsTr("Host logs"); onClicked: hostManager.openLogs() }
        }
        Label { text: qsTr("Pair a connecting device"); font.pixelSize: 20; Layout.leftMargin: 20 }
        Label {
            text: qsTr("On the other computer, add this computer's address with :48989. Enter its displayed PIN here. This authorizes access to this host; reverse access is paired separately in this preview.")
            wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
        }
        RowLayout {
            Layout.leftMargin: 20
            TextField { id: peer; placeholderText: qsTr("Device name"); maximumLength: 64 }
            TextField { id: pin; placeholderText: qsTr("Four-digit PIN"); maximumLength: 4; echoMode: TextInput.Password }
            Button { text: qsTr("Pair"); enabled: hostManager.canPair; onClicked: { hostManager.pair(pin.text, peer.text); pin.clear() } }
        }
        Label {
            text: qsTr("Preview: select a fixed display size before sharing. Live window-size adaptation is not implemented yet.")
            wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.margins: 20
        }
    }
}
