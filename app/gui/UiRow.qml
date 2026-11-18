import QtQuick 2.9
import QtQuick.Controls 2.5
import QtQuick.Controls.impl 2.12
import QtQuick.Layouts 1.3

// One row of a settings group: icon tile, title with an optional explanation,
// the current value, then a chevron or a switch. An editable value has its own
// button; clicking the rest of a switch row emits switched(). A row with an
// infoAccessibleName shows an information button that emits infoClicked().
ItemDelegate {
    id: row
    property url iconSource
    property string title
    property string detail
    property string value
    property bool valueEditable: false
    property string valueAccessibleName: title
    property bool switchable: false
    property bool on: false
    property bool destructive: false
    // A plain fact row: no chevron, no hover and no click.
    property bool readOnly: false
    property bool showChevron: !switchable && !readOnly && !destructive
    // Turns the chevron down while the row's section is expanded in place.
    property bool expanded: false
    // Whether the row draws its separator from the next row.
    property bool divider: true
    property string infoAccessibleName
    signal switched(bool value)
    signal valueClicked()
    signal infoClicked()
    width: parent ? parent.width : implicitWidth
    implicitHeight: Math.max(52, layout.implicitHeight + 20)
    leftPadding: 16; rightPadding: 14; topPadding: 10; bottomPadding: 10
    hoverEnabled: !readOnly
    focusPolicy: readOnly ? Qt.NoFocus : Qt.TabFocus
    Accessible.role: switchable ? Accessible.CheckBox : readOnly ? Accessible.StaticText : Accessible.Button
    Accessible.name: title + (value.length > 0 ? " · " + value : "")
    Accessible.checkable: switchable
    Accessible.checked: on
    onClicked: if (switchable && enabled) switched(!on)
    background: Rectangle {
        color: !row.readOnly && (row.down || row.hovered) ? ui.raised : "transparent"
        border.width: row.activeFocus && row.visualFocus ? 2 : 0
        border.color: ui.accent
        Rectangle {
            visible: row.divider
            anchors.bottom: parent.bottom; anchors.right: parent.right; anchors.rightMargin: 16
            anchors.left: parent.left; anchors.leftMargin: row.iconSource.toString().length > 0 ? 64 : 16
            height: 1; color: ui.line
        }
    }
    contentItem: RowLayout {
        id: layout
        spacing: 12
        opacity: row.enabled ? 1 : 0.45
        UiIconTile {
            visible: row.iconSource.toString().length > 0
            source: row.iconSource
            color: row.destructive ? Qt.rgba(ui.danger.r, ui.danger.g, ui.danger.b, 0.14) : ui.tint
            iconColor: row.destructive ? ui.danger : ui.accent
            Layout.alignment: Qt.AlignVCenter
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: 2
            Label {
                text: row.title; textFormat: Text.PlainText
                color: row.destructive ? ui.danger : ui.text; font.pixelSize: ui.body
                wrapMode: row.valueEditable ? Text.NoWrap : Text.WordWrap
                elide: row.valueEditable ? Text.ElideRight : Text.ElideNone
                Layout.fillWidth: true
            }
            Label {
                visible: text.length > 0
                text: row.detail; textFormat: Text.PlainText
                color: ui.muted; font.pixelSize: ui.small
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
        }
        Label {
            visible: !row.valueEditable && text.length > 0
            text: row.value; textFormat: Text.PlainText
            color: ui.muted; font.pixelSize: ui.body
            horizontalAlignment: Text.AlignRight; elide: Text.ElideRight
            Layout.maximumWidth: row.width * 0.45
        }
        UiButton {
            objectName: row.objectName.length > 0 ? row.objectName + "Value" : ""
            visible: row.valueEditable && text.length > 0
            text: row.value
            tinted: true
            font.pixelSize: ui.body
            Layout.maximumWidth: row.width * 0.45
            Accessible.name: row.valueAccessibleName
            Accessible.description: row.value
            onClicked: row.valueClicked()
        }
        UiIconButton {
            objectName: row.objectName.length > 0 ? row.objectName + "Info" : ""
            visible: row.infoAccessibleName.length > 0
            source: "qrc:/res/ui/info.svg"
            implicitWidth: 32; implicitHeight: 32; iconSize: 18
            Accessible.name: row.infoAccessibleName
            ToolTip.visible: hovered
            ToolTip.delay: 800
            ToolTip.text: row.infoAccessibleName
            onClicked: row.infoClicked()
        }
        Switch {
            visible: row.switchable
            // Display only: the row decides, so the switch never breaks its binding.
            checkable: false
            checked: row.on
            focusPolicy: Qt.NoFocus
            enabled: row.enabled
            padding: 0
            onClicked: row.clicked()
        }
        IconImage {
            objectName: "rowChevron"
            visible: row.showChevron
            source: "qrc:/res/ui/chevron.svg"; color: ui.muted
            sourceSize.width: 16; sourceSize.height: 16
            rotation: row.expanded ? 90 : row.mirrored ? 180 : 0
            Behavior on rotation { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        }
    }
}
