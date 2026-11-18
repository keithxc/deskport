import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.2

// A message, question or wait in the shared floating panel.
NavigableDialog {
    id: dialog

    property alias text: dialogLabel.dialogText
    property alias showSpinner: dialogSpinner.visible
    // Kept for callers that set it; panels follow the theme instead of an image.
    property url imageSrc

    property string helpText
    property string helpUrl : "https://github.com/moonlight-stream/moonlight-docs/wiki/Troubleshooting"
    property string helpTextSeparator : " "
    explicitFocusCycle: true
    focusBodyItem: dialogLabel

    onOpened: {
        // Force keyboard focus on the label so keyboard navigation works
        dialogLabel.forceActiveFocus()
        messageScroll.scrollBy(-messageScroll.contentHeight)
    }

    contentItem: ScrollView {
        id: messageScroll
        objectName: "messageScroll"
        clip: true
        implicitHeight: messageRow.implicitHeight
        height: Math.max(0, Math.min(implicitHeight, dialog.height - dialog.topPadding - dialog.bottomPadding
                                    - (dialog.header ? dialog.header.height : 0) - (dialog.footer ? dialog.footer.height : 0)))
        contentWidth: availableWidth
        contentHeight: messageRow.implicitHeight
        function scrollBy(amount) {
            var view = contentItem
            if (view && view.contentY !== undefined)
                view.contentY = Math.max(0, Math.min(Math.max(0, view.contentHeight - view.height), view.contentY + amount))
        }

        RowLayout {
            id: messageRow
            width: messageScroll.availableWidth
            spacing: 14

            BusyIndicator {
                id: dialogSpinner
                visible: false
                implicitWidth: 36; implicitHeight: 36
            }

            Label {
                property string dialogText

                id: dialogLabel
                objectName: "messageText"
                text: dialogText + ((helpText && (standardButtons & Dialog.Help)) ? (helpTextSeparator + helpText) : "")
                color: dialog.panelTheme.text
                wrapMode: Text.Wrap
                Layout.fillWidth: true

                Keys.onTabPressed: dialog.cycleFocus(dialogLabel, false)
                Keys.onBacktabPressed: dialog.cycleFocus(dialogLabel, true)
                Keys.onPressed: function(event) {
                    var amount
                    switch (event.key) {
                    case Qt.Key_Down: amount = 40; break
                    case Qt.Key_Up: amount = -40; break
                    case Qt.Key_PageDown: amount = messageScroll.availableHeight * 0.9; break
                    case Qt.Key_PageUp: amount = -messageScroll.availableHeight * 0.9; break
                    case Qt.Key_Home: amount = -messageScroll.contentHeight; break
                    case Qt.Key_End: amount = messageScroll.contentHeight; break
                    default: return
                    }
                    messageScroll.scrollBy(amount)
                    event.accepted = true
                }

                Keys.onReturnPressed: {
                    accept()
                }

                Keys.onEnterPressed: {
                    accept()
                }

                Keys.onEscapePressed: {
                    reject()
                }
            }
        }
    }

    footer: DialogButtonBox {
        id: dialogButtonBox
        visible: count > 0
        standardButtons: dialog.standardButtons
        alignment: Qt.AlignRight
        spacing: 8
        padding: 20; topPadding: 4
        background: Item {}
        delegate: UiButton {
            id: messageButton
            objectName: accepting ? "dialogAcceptButton" : "dialogRejectButton"
            readonly property bool accepting: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
                                              || DialogButtonBox.buttonRole === DialogButtonBox.YesRole
            filled: accepting
            destructive: accepting && dialog.destructive
            Keys.onTabPressed: dialog.cycleFocus(messageButton, false)
            Keys.onBacktabPressed: dialog.cycleFocus(messageButton, true)
        }

        onHelpRequested: {
            Qt.openUrlExternally(helpUrl)
            close()
        }
    }
}
