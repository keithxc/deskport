import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Controls.impl 2.12
import QtQuick.Layouts 1.3
import StreamingPreferences 1.0
import Manual 1.0

// The manual that ships inside the application: the shared core file, bundled as
// a resource. Nothing is fetched at runtime, so it always matches this build.
// Chapters are cards that open one at a time, like the mobile clients.
UiPage {
    id: page
    objectName: qsTr("Manual")
    property int openChapter: 0
    // Re-read when the language changes so an open page follows the setting.
    readonly property var chapters: {
        StreamingPreferences.language
        return Manual.chapters(StreamingPreferences.manualLanguage())
    }
    readonly property var icons: ({ about: "ui/info", share: "share-screen", connect: "ui/link", organize: "devices-grid",
                                    windows: "ui/reconnect", shortcuts: "ui/input", fullscreen: "ui/fullscreen",
                                    picture: "ui/picture", input: "ui/input", quality: "ui/traffic",
                                    browser: "ui/link", diagnostics: "ui/diagnostics", trouble: "ui/wrench" })
    function iconFor(id) { return "qrc:/res/" + (icons[id] || "ui/guide") + ".svg" }
    Repeater {
        model: page.chapters
        Rectangle {
            id: card
            objectName: "chapter" + index
            readonly property bool open: page.openChapter === index
            Layout.fillWidth: true
            implicitHeight: body.implicitHeight + 28
            radius: 16
            color: ui.surface
            border.color: card.open ? Qt.rgba(ui.accent.r, ui.accent.g, ui.accent.b, 0.45) : ui.line
            Behavior on implicitHeight { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            clip: true
            AbstractButton {
                id: chapterHeader
                objectName: "chapterHeader" + index
                x: 0; y: 0; width: parent.width; height: headerRow.implicitHeight + 28
                hoverEnabled: true
                Accessible.role: Accessible.Button
                Accessible.name: modelData.title
                Accessible.description: card.open ? "" : (modelData.steps.length > 0 ? modelData.steps[0] : "")
                onClicked: page.openChapter = card.open ? -1 : index
                background: Rectangle {
                    radius: 16
                    color: chapterHeader.hovered ? Qt.rgba(ui.text.r, ui.text.g, ui.text.b, 0.035) : "transparent"
                    border.width: chapterHeader.visualFocus ? 2 : 0; border.color: ui.accent
                }
            }
            ColumnLayout {
                id: body
                x: 16; y: 14; width: parent.width - 32
                spacing: 12
                RowLayout {
                    id: headerRow
                    Layout.fillWidth: true; spacing: 12
                    UiIconTile { objectName: "chapterIcon" + index; source: page.iconFor(modelData.id); Layout.alignment: Qt.AlignVCenter }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2
                        Label { text: modelData.title; textFormat: Text.PlainText; color: ui.text; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                        // The first step as a one-line preview while closed.
                        Label {
                            visible: !card.open && modelData.steps.length > 0
                            text: modelData.steps.length > 0 ? modelData.steps[0] : ""
                            textFormat: Text.PlainText; color: ui.muted; font.pixelSize: ui.small
                            elide: Text.ElideRight; maximumLineCount: 1; Layout.fillWidth: true
                        }
                    }
                    IconImage {
                        objectName: "chapterChevron" + index
                        source: "qrc:/res/ui/chevron.svg"; color: ui.muted
                        sourceSize.width: 16; sourceSize.height: 16
                        rotation: card.open ? 90 : Qt.application.layoutDirection === Qt.RightToLeft ? 180 : 0
                        Behavior on rotation { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                    }
                }
                ColumnLayout {
                    visible: card.open; spacing: 10; Layout.fillWidth: true
                    // The layout mirrors this margin automatically in RTL.
                    Layout.leftMargin: 44; Layout.bottomMargin: 2
                    Repeater {
                        model: modelData.steps
                        RowLayout {
                            Layout.fillWidth: true; spacing: 10
                            Rectangle {
                                objectName: "chapterStep" + index
                                Layout.alignment: Qt.AlignTop
                                implicitWidth: 22; implicitHeight: 22; radius: 11
                                color: ui.accent
                                Label { anchors.centerIn: parent; text: index + 1; color: ui.accentText; font.pixelSize: 12; font.weight: Font.DemiBold }
                            }
                            Label {
                                text: modelData; textFormat: Text.PlainText
                                color: ui.text; wrapMode: Text.WordWrap; Layout.fillWidth: true
                                topPadding: 2; lineHeight: 1.15
                            }
                        }
                    }
                }
            }
        }
    }
}
