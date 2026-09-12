import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
ScrollView {
    id: page
    clip: true
    property string heading
    property string description
    default property alias body: bodyColumn.data
    contentItem: Flickable {
        contentWidth: width
        contentHeight: pageColumn.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ColumnLayout {
        id: pageColumn
        width: page.availableWidth
        spacing: 22
        Item { height: 4; Layout.fillWidth: true }
        ColumnLayout {
            Layout.fillWidth: true; Layout.leftMargin: 28; Layout.rightMargin: 28; spacing: 8
            Label { text: page.heading; font.pixelSize: ui.heading; font.weight: Font.DemiBold; color: ui.text; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Label { text: page.description; color: ui.muted; font.pixelSize: ui.body; Layout.fillWidth: true; wrapMode: Text.WordWrap; visible: text.length > 0 }
        }
        ColumnLayout { id: bodyColumn; spacing: 16; Layout.fillWidth: true; Layout.leftMargin: 28; Layout.rightMargin: 28 }
        Item { height: 24; Layout.fillWidth: true }
    }
    }
}
