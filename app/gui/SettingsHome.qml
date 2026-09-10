import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import StreamingPreferences 1.0
import SystemProperties 1.0

UiPage {
    objectName: qsTr("Settings")
    heading: qsTr("Make DeskPort your own.")
    description: qsTr("Language changes apply immediately. Connection preferences apply to your next connection from this computer.")
    function save() { StreamingPreferences.save() }
    UiCard {
        ColumnLayout {
            anchors.fill: parent; spacing: 10
            Label { text: qsTr("Language"); color: ui.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            ComboBox {
                id: languageChoice
                objectName: "languageChoice"
                Layout.fillWidth: true
                textRole: "label"
                popup: Popup {
                    y: languageChoice.height
                    width: languageChoice.width
                    height: Math.min(340, contentItem.implicitHeight + topPadding + bottomPadding)
                    margins: 8
                    padding: 4
                    contentItem: ListView {
                        clip: true
                        implicitHeight: contentHeight
                        model: languageChoice.popup.visible ? languageChoice.delegateModel : null
                        currentIndex: languageChoice.highlightedIndex
                        ScrollIndicator.vertical: ScrollIndicator {}
                    }
                }
                model: [
                    { label: qsTr("Follow system"), value: StreamingPreferences.LANG_AUTO },
                    { label: "English", value: StreamingPreferences.LANG_EN },
                    { label: "简体中文", value: StreamingPreferences.LANG_ZH_CN },
                    { label: "繁體中文", value: StreamingPreferences.LANG_ZH_TW },
                    { label: "日本語", value: StreamingPreferences.LANG_JA },
                    { label: "한국어", value: StreamingPreferences.LANG_KO },
                    { label: "Deutsch", value: StreamingPreferences.LANG_DE },
                    { label: "Français", value: StreamingPreferences.LANG_FR },
                    { label: "Español", value: StreamingPreferences.LANG_ES },
                    { label: "Italiano", value: StreamingPreferences.LANG_IT },
                    { label: "Português", value: StreamingPreferences.LANG_PT },
                    { label: "Русский", value: StreamingPreferences.LANG_RU },
                    { label: "Nederlands", value: StreamingPreferences.LANG_NL },
                    { label: "Polski", value: StreamingPreferences.LANG_PL },
                    { label: "Čeština", value: StreamingPreferences.LANG_CS },
                    { label: "Svenska", value: StreamingPreferences.LANG_SV },
                    { label: "Norsk bokmål", value: StreamingPreferences.LANG_NB_NO },
                    { label: "Türkçe", value: StreamingPreferences.LANG_TR },
                    { label: "Magyar", value: StreamingPreferences.LANG_HU },
                    { label: "Ελληνικά", value: StreamingPreferences.LANG_EL },
                    { label: "Tiếng Việt", value: StreamingPreferences.LANG_VI },
                    { label: "ภาษาไทย", value: StreamingPreferences.LANG_TH }
                ]
                currentIndex: {
                    for (var i = 0; i < model.length; ++i)
                        if (model[i].value === StreamingPreferences.language) return i
                    return -1
                }
                onActivated: function(index) {
                    var value = model[index].value
                    if (StreamingPreferences.language === value) return
                    StreamingPreferences.language = value
                    save()
                    if (!StreamingPreferences.retranslate())
                        ToolTip.show(qsTr("Restart DeskPort to apply this language."), 5000)
                    else if (typeof window !== "undefined")
                        window.clearOnBack = true
                }
            }
            Label { text: qsTr("Saved on this computer. Missing translations appear in English."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
    TabBar {
        id: sections
        objectName: "settingsSections"
        Layout.fillWidth: true
        Repeater {
            model: ListModel {
                ListElement { label: qsTr("Picture") }
                ListElement { label: qsTr("Input") }
                ListElement { label: qsTr("Sound") }
                ListElement { label: qsTr("Connections") }
                ListElement { label: qsTr("Advanced") }
            }
            TabButton { text: label }
        }
    }
    UiCard {
        visible: sections.currentIndex === 0

        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Picture"); color: ui.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Switch { text: qsTr("Match the client window resolution"); checked: StreamingPreferences.adaptiveResolution; onClicked: { StreamingPreferences.adaptiveResolution=checked; save() } }
            Label { text: qsTr("Uses the built-in virtual display on a bound Mac. Resizing briefly reconnects the picture and keeps your apps open. Other hosts use the resolution below."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
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
                objectName: "windowModeChoice"
                textRole: "label"
                model: ListModel {
                    ListElement { label: qsTr("Full screen") }
                    ListElement { label: qsTr("Borderless full screen") }
                    ListElement { label: qsTr("Window") }
                }
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
        visible: sections.currentIndex === 1

        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Keyboard & pointer"); color: ui.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Switch { text: qsTr("Use a desktop-style pointer"); checked: StreamingPreferences.absoluteMouseMode; onClicked: { StreamingPreferences.absoluteMouseMode=checked; save() } }
            Switch { objectName: "localCursorSwitch"; text: qsTr("Always show a local pointer in desktop mode"); checked: StreamingPreferences.showLocalCursor; enabled: StreamingPreferences.absoluteMouseMode; onClicked: { StreamingPreferences.showLocalCursor=checked; save() } }
            Label { text: qsTr("Keeps the pointer visible if the host hides its cursor. Turn this off if you see two pointers."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Switch { text: qsTr("Reverse scrolling direction"); checked: StreamingPreferences.reverseScrollDirection; onClicked: { StreamingPreferences.reverseScrollDirection=checked; save() } }
            Label { text: qsTr("Send system shortcuts to the remote computer"); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            ComboBox {
                objectName: "systemKeysChoice"
                textRole: "label"
                model: ListModel {
                    ListElement { label: qsTr("Never") }
                    ListElement { label: qsTr("Only in full screen") }
                    ListElement { label: qsTr("Always") }
                }
                currentIndex: StreamingPreferences.captureSysKeysMode
                onActivated: function(index) { StreamingPreferences.captureSysKeysMode=index; save() }
                Layout.preferredWidth: 250
            }
            Label { text: qsTr("On a Mac host, Super / Windows sends Command and Alt sends Option. Choose Always to forward Super + Space in a window. Changes apply on the next connection."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: qsTr("Release remote input with Ctrl + Alt + Shift + Z."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
    UiCard {
        visible: sections.currentIndex === 2

        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Sound"); color: ui.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Switch { text: qsTr("Mute when DeskPort loses focus"); checked: StreamingPreferences.muteOnFocusLoss; onClicked: { StreamingPreferences.muteOnFocusLoss=checked; save() } }
            Switch { text: qsTr("Also play audio on the host"); checked: StreamingPreferences.playAudioOnHost; onClicked: { StreamingPreferences.playAudioOnHost=checked; save() } }
        }
    }
    UiCard {
        visible: sections.currentIndex === 3

        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Connections"); color: ui.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Switch { text: qsTr("Discover nearby devices"); checked: StreamingPreferences.enableMdns; onClicked: { StreamingPreferences.enableMdns=checked; save() } }
            Switch { text: qsTr("Keep this computer awake while connected"); checked: StreamingPreferences.keepAwake; onClicked: { StreamingPreferences.keepAwake=checked; save() } }
            UiButton { text: qsTr("Manage saved access"); onClicked: navigateTo("qrc:/gui/BindView.qml", "BindView") }
        }
    }
    UiCard {
        visible: sections.currentIndex === 4

        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Advanced & support"); color: ui.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Label { text: qsTr("Custom resolutions, codecs, HDR, surround sound and controller options remain available in advanced settings."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                UiButton { text: qsTr("Advanced settings"); onClicked: navigateTo("qrc:/gui/SettingsView.qml", "SettingsView") }
                UiButton { text: qsTr("Permission guide"); onClicked: navigateTo("qrc:/gui/SetupView.qml", "SetupView") }
            }
            UiButton { text: qsTr("Report a problem"); visible: SystemProperties.hasBrowser; onClicked: Qt.openUrlExternally("https://github.com/keithxc/deskport/issues") }
        }
    }
}
