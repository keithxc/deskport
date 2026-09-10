import QtQuick 2.9
import QtQuick.Controls 2.2
Button {
    id: control
    property color labelColor: highlighted ? "#14271e" : "#f1f4f3"
    contentItem: Label {
        text: control.text
        font: control.font
        color: control.labelColor
        opacity: control.enabled ? 1 : 0.45
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
