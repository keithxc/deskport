import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import StreamingPreferences 1.0
import SystemProperties 1.0

UiPage {
    objectName: qsTr("Settings")
    heading: qsTr("Settle into your workspace.")
    description: qsTr("These preferences apply when you connect from this computer. Changes are saved automatically and take effect on your next connection.")
    function save() { StreamingPreferences.save() }
    UiCard {
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Picture"); color: ui.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Label { text: qsTr("Resolution"); color: ui.muted }
            ComboBox {
                id: resolution; objectName: "resolutionChoice"
                model: ["1920 × 1080", "2560 × 1440", "2880 × 1800", "3840 × 2160"]
                property var widths: [1920,2560,2880,3840]
                property var heights: [1080,1440,1800,2160]
                currentIndex: { for (var i=0; i<widths.length; i++) if (StreamingPreferences.width===widths[i] && StreamingPreferences.height===heights[i]) return i; return -1 }
                displayText: currentIndex < 0 ? StreamingPreferences.width + " × " + StreamingPreferences.height : currentText
                onActivated: function(index) { StreamingPreferences.width=widths[index]; StreamingPreferences.height=heights[index]; save() }
                Layout.preferredWidth: 250
            }
            Label { text: qsTr("Connection window"); color: ui.muted }
            ComboBox {
                model: [qsTr("Full screen"), qsTr("Borderless full screen"), qsTr("Window")]
                currentIndex: StreamingPreferences.windowMode
                onActivated: function(index) { StreamingPreferences.windowMode=index; save() }
                Layout.preferredWidth: 250
            }
            Label { text: qsTr("Frame rate"); color: ui.muted }
            ComboBox {
                model: ["30 fps", "60 fps", "90 fps", "120 fps"]
                property var rates: [30,60,90,120]
                currentIndex: rates.indexOf(StreamingPreferences.fps)
                displayText: currentIndex < 0 ? StreamingPreferences.fps + " fps" : currentText
                onActivated: function(index) { StreamingPreferences.fps=rates[index]; save() }
                Layout.preferredWidth: 250
            }
            Label { text: qsTr("Bandwidth · %1 Mbps").arg(Math.round(StreamingPreferences.bitrateKbps/1000)); color: ui.muted }
            Slider { objectName: "bitrateSlider"; from: 5; to: 100; stepSize: 1; value: StreamingPreferences.bitrateKbps/1000; Layout.fillWidth: true; onMoved: { StreamingPreferences.bitrateKbps=Math.round(value)*1000; save() } }
            Label { text: qsTr("Higher values improve detail and use more network capacity. Keep your existing advanced values unless you move this slider."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Switch { text: qsTr("Synchronize frames to this display"); checked: StreamingPreferences.enableVsync; onClicked: { StreamingPreferences.enableVsync=checked; save() } }
        }
    }
    UiCard {
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Keyboard & pointer"); color: ui.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Switch { text: qsTr("Use a desktop-style pointer"); checked: StreamingPreferences.absoluteMouseMode; onClicked: { StreamingPreferences.absoluteMouseMode=checked; save() } }
            Switch { text: qsTr("Reverse scrolling direction"); checked: StreamingPreferences.reverseScrollDirection; onClicked: { StreamingPreferences.reverseScrollDirection=checked; save() } }
            Label { text: qsTr("Send system shortcuts to the remote computer"); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            ComboBox { model: [qsTr("Never"), qsTr("Only in full screen"), qsTr("Always")]; currentIndex: StreamingPreferences.captureSysKeysMode; onActivated: function(index) { StreamingPreferences.captureSysKeysMode=index; save() } Layout.preferredWidth: 250 }
            Label { text: qsTr("Release remote input with Ctrl + Alt + Shift + Z."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
    UiCard {
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Sound"); color: ui.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Switch { text: qsTr("Mute when DeskPort loses focus"); checked: StreamingPreferences.muteOnFocusLoss; onClicked: { StreamingPreferences.muteOnFocusLoss=checked; save() } }
            Switch { text: qsTr("Also play audio on the host"); checked: StreamingPreferences.playAudioOnHost; onClicked: { StreamingPreferences.playAudioOnHost=checked; save() } }
        }
    }
    UiCard {
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Connections"); color: ui.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Switch { text: qsTr("Discover nearby devices"); checked: StreamingPreferences.enableMdns; onClicked: { StreamingPreferences.enableMdns=checked; save() } }
            Switch { text: qsTr("Keep this computer awake while connected"); checked: StreamingPreferences.keepAwake; onClicked: { StreamingPreferences.keepAwake=checked; save() } }
            Button { text: qsTr("Manage saved access"); onClicked: navigateTo("qrc:/gui/BindView.qml", "BindView") }
        }
    }
    UiCard {
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Advanced & support"); color: ui.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Label { text: qsTr("Custom resolutions, codecs, HDR, surround sound and controller options remain available in advanced settings."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Button { text: qsTr("Advanced settings"); onClicked: navigateTo("qrc:/gui/SettingsView.qml", "SettingsView") }
                Button { text: qsTr("Permission guide"); onClicked: navigateTo("qrc:/gui/SetupView.qml", "SetupView") }
            }
            Button { text: qsTr("Report a problem"); visible: SystemProperties.hasBrowser; onClicked: Qt.openUrlExternally("https://github.com/keithxc/deskport/issues") }
        }
    }
}
