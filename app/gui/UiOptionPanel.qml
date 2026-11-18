import QtQuick 2.9
import QtQuick.Controls 2.5
import QtQuick.Controls.impl 2.12
import QtQuick.Layouts 1.3

// A floating list of options: each is a string or {label, detail}. Choosing one
// reports it through chosen() and closes the panel.
NavigableDialog {
    id: options
    property var model: []
    property int currentIndex: -1
    // An optional explanation shown above the options.
    property string note
    signal chosen(int index)
    function choose(index) { close(); chosen(index) }
    function labelAt(index) { var o = model[index]; return o === undefined ? "" : typeof o === "string" ? o : o.label }
    function detailAt(index) { var o = model[index]; return o === undefined || typeof o === "string" ? "" : (o.detail || "") }
    padding: 0; topPadding: 0; bottomPadding: 12
    onOpened: {
        list.currentIndex = Math.max(0, currentIndex)
        list.positionViewAtIndex(list.currentIndex, ListView.Contain)
        list.forceActiveFocus()
    }
    contentItem: ColumnLayout {
        spacing: 0
        Label {
            visible: text.length > 0
            text: options.note; textFormat: Text.PlainText
            color: ui.muted; font.pixelSize: ui.small; wrapMode: Text.WordWrap
            leftPadding: 20; rightPadding: 20; bottomPadding: 8
            Layout.fillWidth: true
        }
        ListView {
            id: list
            objectName: "optionList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: contentHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: options.model.length
            keyNavigationEnabled: true
            activeFocusOnTab: true
            Keys.onReturnPressed: if (currentIndex >= 0) options.choose(currentIndex)
            Keys.onEnterPressed: if (currentIndex >= 0) options.choose(currentIndex)
            ScrollIndicator.vertical: ScrollIndicator {}
            delegate: ItemDelegate {
                id: option
                objectName: "option" + index
                width: list.width
                readonly property bool current: index === options.currentIndex
                leftPadding: 20; rightPadding: 16; topPadding: 10; bottomPadding: 10
                Accessible.name: options.labelAt(index)
                Accessible.role: Accessible.RadioButton
                Accessible.checked: current
                onClicked: options.choose(index)
                background: Rectangle {
                    color: option.down || option.hovered ? ui.raised : option.current ? ui.tint : "transparent"
                    border.width: list.activeFocus && index === list.currentIndex ? 2 : 0
                    border.color: ui.accent
                }
                contentItem: RowLayout {
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2
                        Label { text: options.labelAt(index); textFormat: Text.PlainText; color: option.current ? ui.accent : ui.text; font.weight: option.current ? Font.DemiBold : Font.Normal; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Label { visible: text.length > 0; text: options.detailAt(index); textFormat: Text.PlainText; color: ui.muted; font.pixelSize: ui.small; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                    IconImage {
                        source: "qrc:/res/ui/check.svg"; color: ui.accent
                        opacity: option.current ? 1 : 0
                        sourceSize.width: 18; sourceSize.height: 18
                    }
                }
            }
        }
    }
}
