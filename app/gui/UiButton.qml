import QtQuick 2.9
import QtQuick.Controls 2.2
Button {
    id: deskButton
    implicitHeight: 40
    implicitWidth: Math.max(80, contentItem.implicitWidth + 28)
    leftPadding: 14; rightPadding: 14
    topInset: 0; bottomInset: 0; leftInset: 0; rightInset: 0
    hoverEnabled: true
    background: Rectangle {
        radius: 8
        color: deskButton.highlighted ? ui.accent : deskButton.down || deskButton.hovered ? ui.raised : deskButton.flat ? "transparent" : ui.surface
        border.color: deskButton.activeFocus ? ui.accent : deskButton.flat ? "transparent" : ui.line
        border.width: deskButton.activeFocus ? 2 : 1
        opacity: deskButton.enabled ? 1 : 0.45
    }
    contentItem: Text {
        text: deskButton.text; font: deskButton.font; color: deskButton.highlighted ? ui.accentText : ui.text
        opacity: deskButton.enabled ? 1 : 0.45
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
