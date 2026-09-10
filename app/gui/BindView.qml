import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

ScrollView {
    objectName: qsTr("Bind device")
    ColumnLayout {
        width: parent.width
        spacing: 16
        Label { text: qsTr("Bind once. Connect in either direction."); font.pixelSize: 26; Layout.margins: 20 }
        Label {
            text: qsTr("Enter the other computer's IP address or domain. It must have DeskPort open. The other person confirms once; both computers save each other and enable desktop sharing.")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
        }
        RowLayout {
            Layout.leftMargin: 20; Layout.rightMargin: 20
            TextField { id: address; placeholderText: qsTr("IP address or domain"); Layout.preferredWidth: 320; enabled: !peerManager.busy }
            Button { text: qsTr("Request binding"); enabled: !peerManager.busy && address.text.trim().length > 0; onClicked: peerManager.request(address.text) }
            Button { text: qsTr("Cancel request"); enabled: peerManager.busy; onClicked: peerManager.cancel() }
        }
        Label {
            text: peerManager.status; textFormat: Text.PlainText
            wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
        }
        Label {
            text: qsTr("Binding port: %1 (default 48991). Use address:port for a custom binding endpoint. System permissions are still required on each computer.").arg(peerManager.port)
            wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
        }
        Label { text: qsTr("Saved bindings"); font.pixelSize: 20; Layout.leftMargin: 20 }
        Repeater {
            model: peerManager.peers
            RowLayout {
                Layout.leftMargin: 20; Layout.rightMargin: 20
                Label { text: modelData.name + " — " + modelData.address + (modelData.ready ? qsTr(" · Bound") : qsTr(" · Incomplete")); textFormat: Text.PlainText }
                Button {
                    text: qsTr("Remove its access here"); enabled: !peerManager.busy
                    onClicked: { removeDialog.fingerprint = modelData.fingerprint; removeDialog.open() }
                }
            }
        }
        Label {
            text: qsTr("Removing access here prevents that device from controlling this computer. Remove the binding on the other computer too to revoke both directions.")
            wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.margins: 20
        }
    }
    Dialog {
        id: removeDialog
        property string fingerprint: ""
        title: qsTr("Remove this device's access?")
        modal: true; standardButtons: Dialog.Yes | Dialog.No
        onAccepted: peerManager.revoke(fingerprint)
    }
}
