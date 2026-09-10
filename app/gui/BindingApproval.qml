import QtQuick 2.9
import QtQuick.Controls 2.2

Dialog {
    id: bindingApproval
    objectName: "bindingApproval"
    property var manager
    property var appWindow
    property string transaction: ""
    property string peerText: ""
    title: qsTr("Allow mutual desktop access?")
    modal: true
    anchors.centerIn: parent
    width: Math.min(appWindow.width - 40, 540)
    closePolicy: Popup.NoAutoClose
    standardButtons: Dialog.Yes | Dialog.No
    onAccepted: manager.approve(transaction)
    onRejected: manager.reject(transaction)
    contentItem: Label {
        textFormat: Text.PlainText
        text: bindingApproval.peerText + "\n\n" + qsTr("Allow this device and this computer to view and control each other? DeskPort sharing will start on both computers; existing DeskPort sessions may briefly disconnect. Accept only a request you are expecting.")
        wrapMode: Text.WordWrap
    }
    Connections {
        target: bindingApproval.manager
        function onIncomingRequest() {
            bindingApproval.transaction = bindingApproval.manager.requestId
            bindingApproval.peerText = bindingApproval.manager.pendingName
            bindingApproval.open()
            console.info("Binding: approval dialog opened:", bindingApproval.visible)
            bindingApproval.appWindow.show()
            bindingApproval.appWindow.raise()
            bindingApproval.appWindow.requestActivate()
        }
        function onChanged() {
            if (bindingApproval.visible && bindingApproval.manager.requestId !== bindingApproval.transaction)
                bindingApproval.close()
        }
    }
}
