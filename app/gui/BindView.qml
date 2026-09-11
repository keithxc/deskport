import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

UiPage {
    id: page
    objectName: qsTr("Add a device")
    heading: qsTr("One confirmation. Both directions.")
    description: qsTr("Keep DeskPort open on both computers. Enter an address, then approve the request on the other device.")
    function setAddress(value) { address.text = value }
    UiCard {
        ColumnLayout {
            anchors.fill: parent; spacing: 14
            Label { text: qsTr("Where do you want to connect?"); color: ui.text; font.pixelSize: 19; font.weight: Font.DemiBold }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: address; objectName: "bindingAddress"; placeholderText: qsTr("IP address or computer name"); Layout.fillWidth: true; enabled: !peerManager.busy; onAccepted: if (!peerManager.busy && text.trim().length) peerManager.request(text) }
                UiButton { text: qsTr("Send request"); highlighted: true; enabled: !peerManager.busy && address.text.trim().length > 0; onClicked: peerManager.request(address.text) }
            }
            RowLayout {
                Layout.fillWidth: true
                BusyIndicator { running: peerManager.busy; visible: running; implicitWidth: 28; implicitHeight: 28 }
                Label { text: peerManager.status; textFormat: Text.PlainText; color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true; Accessible.role: Accessible.StaticText }
                UiButton { text: qsTr("Cancel"); visible: peerManager.busy; onClicked: peerManager.cancel() }
            }
            Label { text: qsTr("Approval lets both computers view and control each other. System permissions are still required on each device."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
    Label { text: qsTr("Saved access"); font.pixelSize: 20; font.weight: Font.DemiBold; color: ui.text }
    Label { visible: peerManager.peers.length === 0; text: qsTr("Your approved devices will appear here."); color: ui.muted }
    Repeater {
        model: peerManager.peers
        UiCard {
            ColumnLayout {
                anchors.fill: parent; spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: modelData.name; textFormat: Text.PlainText; color: ui.text; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                    Label { text: modelData.ready ? qsTr("Bound both ways") : qsTr("Incomplete"); color: modelData.ready ? ui.accent : ui.warning }
                }
                Label { text: modelData.address; textFormat: Text.PlainText; color: ui.muted }
                UiButton {
                    text: qsTr("Edit device"); enabled: !peerManager.busy
                    onClicked: {
                        editDialog.fingerprint = modelData.fingerprint
                        deviceName.text = modelData.name
                        deviceAddress.text = modelData.address
                        hostPort.value = modelData.hostPort
                        bindingPort.value = modelData.bindingPort
                        editError.text = ""
                        editDialog.open()
                    }
                }
                UiButton { text: qsTr("Remove access to this computer"); enabled: !peerManager.busy; onClicked: { removeDialog.fingerprint = modelData.fingerprint; removeDialog.deviceName = modelData.name; removeDialog.open() } }
            }
        }
    }
    UiCard {
        ColumnLayout {
            anchors.fill: parent; spacing: 10
            Label { text: qsTr("Connecting another way?"); color: ui.text; font.pixelSize: 17; font.weight: Font.DemiBold }
            Label { text: qsTr("For a custom binding port, enter address:port. Moonlight and independent Sunshine hosts use legacy pairing instead."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            UiButton { text: qsTr("Add a legacy host"); onClicked: addPcDialog.open() }
        }
    }
    property Dialog editPrompt: Dialog {
        id: editDialog
        property string fingerprint: ""
        title: qsTr("Edit device")
        anchors.centerIn: parent
        width: Math.max(280, Math.min(page.width - 32, 460))
        modal: true
        contentItem: ColumnLayout {
            spacing: 10
            Label { text: qsTr("Device name") }
            TextField { id: deviceName; objectName: "editPeerName"; Layout.fillWidth: true; maximumLength: 64 }
            Label { text: qsTr("Domain name or IP address") }
            TextField { id: deviceAddress; objectName: "editPeerAddress"; Layout.fillWidth: true; placeholderText: qsTr("Computer name or IP, without port") }
            Label { text: qsTr("A domain name is saved as entered and resolved again when connecting."); wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: qsTr("Host port") }
            SpinBox { id: hostPort; from: 1024; to: 65514; editable: true; Layout.fillWidth: true }
            Label { text: qsTr("Binding port") }
            SpinBox { id: bindingPort; from: 1; to: 65535; editable: true; Layout.fillWidth: true }
            Label { id: editError; textFormat: Text.PlainText; color: ui.warning; visible: text.length > 0; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                UiButton { text: qsTr("Cancel"); onClicked: editDialog.close() }
                Item { Layout.fillWidth: true }
                UiButton {
                    text: qsTr("Save"); highlighted: true; enabled: !peerManager.busy
                    onClicked: {
                        if (peerManager.editPeer(editDialog.fingerprint, deviceName.text, deviceAddress.text, hostPort.value, bindingPort.value)) editDialog.close()
                        else editError.text = peerManager.status
                    }
                }
            }
        }
    }
    property Dialog removalPrompt: Dialog {
        id: removeDialog
        property string fingerprint: ""
        property string deviceName: ""
        title: qsTr("Remove device access?")
        anchors.centerIn: parent
        width: Math.max(280, Math.min(page.width - 32, 460))
        implicitHeight: contentItem.implicitHeight + 150
        modal: true; standardButtons: Dialog.Yes | Dialog.No
        contentItem: Label { text: qsTr("%1 will no longer be able to control this computer. Remove the binding on the other device too to revoke both directions.").arg(removeDialog.deviceName); textFormat: Text.PlainText; wrapMode: Text.WordWrap }
        onAccepted: peerManager.revoke(fingerprint)
    }
}
