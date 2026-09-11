import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2
import QtQuick.Controls.Material 2.2

import ComputerManager 1.0
import AutoUpdateChecker 1.0
import StreamingPreferences 1.0
import SystemProperties 1.0
import SdlGamepadKeyNavigation 1.0

ApplicationWindow {
    property bool pollingActive: false
    readonly property bool navigationVisible: !stackView.currentItem || stackView.currentItem.hidesNavigation !== true

    // Set by SettingsView to force the back operation to pop all
    // pages except the initial view. This is required when doing
    // a retranslate() because AppView breaks for some reason.
    property bool clearOnBack: false

    id: window
    title: "DeskPort"
    onClosing: function(event) {
        event.accepted = false; window.hide();
    }
    width: 1120
    height: 760
    minimumWidth: 760
    minimumHeight: 560
    font.pixelSize: 14
    UiTheme { id: ui }
    Material.theme: Material.Dark
    Material.accent: ui.accent
    Material.primary: ui.surface
    Material.background: ui.canvas
    Material.foreground: ui.text
    color: ui.canvas

    Component.onCompleted: {
        peerManager.restoreHosts()
        if (initialView === "qrc:/gui/PcView.qml") Qt.callLater(function() {
            if (!hostManager.setupComplete && peerManager.peers.length === 0)
                navigateTo("qrc:/gui/SetupView.qml", "SetupView")
            else if (startSharingPage) navigateTo("qrc:/gui/HostView.qml", "HostView")
        })
        if (startInBackground) return
        // Show the window according to the user's preferences
        if (SystemProperties.hasDesktopEnvironment) {
            if (StreamingPreferences.uiDisplayMode == StreamingPreferences.UI_MAXIMIZED) {
                window.showMaximized()
            }
            else if (StreamingPreferences.uiDisplayMode == StreamingPreferences.UI_FULLSCREEN) {
                window.showFullScreen()
            }
            else {
                window.show()
            }
        } else {
            window.showFullScreen()
        }

        // Display any modal dialogs for configuration warnings
        if (SystemProperties.isWow64) {
            wow64Dialog.open()
        }
        else if (!SystemProperties.hasHardwareAcceleration) {
            if (SystemProperties.isRunningXWayland) {
                xWaylandDialog.open()
            }
            else {
                noHwDecoderDialog.open()
            }
        }

        if (SystemProperties.unmappedGamepads) {
            unmappedGamepadDialog.unmappedGamepads = SystemProperties.unmappedGamepads
            unmappedGamepadDialog.open()
        }
    }
  
    function showDevices() { stackView.pop(null) }
    function showDevicesDuringSession() {
        if (!stackView.currentItem || stackView.currentItem.controlCenterForActiveSession !== true) {
            stackView.push(Qt.resolvedUrl("PcView.qml"), {"controlCenterForActiveSession": true}, StackView.Immediate)
        }
        if (window.windowState === Qt.WindowMinimized) window.showNormal()
        else window.show()
        window.raise()
        window.requestActivate()
    }
    function prepareViewerRecall() {
        if (stackView.currentItem && stackView.currentItem.controlCenterForActiveSession === true)
            stackView.pop(StackView.Immediate)
        window.hide()
    }
    function recallRemoteSession() { hostManager.recallViewer() }
    function goBack() {
        if (clearOnBack) {
            // Pop all items except the first one
            stackView.pop(null)
            clearOnBack = false
        }
        else {
            stackView.pop()
        }
    }

    StackView {
        id: stackView
        initialItem: initialView
        anchors.fill: parent
        anchors.leftMargin: navigationVisible ? navigation.width : 0
        anchors.topMargin: navigationVisible ? 66 : 0
        focus: true

        onCurrentItemChanged: {
            // Ensure focus travels to the next view when going back
            if (currentItem) {
                currentItem.forceActiveFocus()
            }
        }

        Keys.onEscapePressed: {
            if (depth > 1) {
                goBack()
            }
            else {
                window.hide()
            }
        }

        Keys.onBackPressed: {
            if (depth > 1) {
                goBack()
            }
            else {
                window.hide()
            }
        }

        Keys.onMenuPressed: {
            settingsButton.clicked()
        }

        // This is a keypress we've reserved for letting the
        // SdlGamepadKeyNavigation object tell us to show settings
        // when Menu is consumed by a focused control.
        Keys.onHangupPressed: {
            settingsButton.clicked()
        }
    }

    // This timer keeps us polling for 5 minutes of inactivity
    // to allow the user to work with Moonlight on a second display
    // while dealing with configuration issues. This will ensure
    // machines come online even if the input focus isn't on Moonlight.
    Timer {
        id: inactivityTimer
        interval: 5 * 60000
        onTriggered: {
            if (!active && pollingActive) {
                ComputerManager.stopPollingAsync()
                pollingActive = false
            }
        }
    }

    onVisibleChanged: {
        // When we become invisible while streaming is going on,
        // stop polling immediately.
        if (!visible) {
            inactivityTimer.stop()

            if (pollingActive) {
                ComputerManager.stopPollingAsync()
                pollingActive = false
            }
        }
        else if (active) {
            // When we become visible and active again, start polling
            inactivityTimer.stop()

            // Restart polling if it was stopped
            if (!pollingActive) {
                ComputerManager.startPolling()
                pollingActive = true
            }
        }
    }

    onActiveChanged: {
        if (active) {
            // Stop the inactivity timer
            inactivityTimer.stop()

            // Restart polling if it was stopped
            if (!pollingActive) {
                ComputerManager.startPolling()
                pollingActive = true
            }
        }
        else {
            // Start the inactivity timer to stop polling
            // if focus does not return within a few minutes.
            inactivityTimer.restart()
        }
    }

    // Workaround for lack of instanceof in Qt 5.9.
    //
    // Based on https://stackoverflow.com/questions/13923794/how-to-do-a-is-a-typeof-or-instanceof-in-qml
    function qmltypeof(obj, className) { // QtObject, string -> bool
        // className plus "(" is the class instance without modification
        // className plus "_QML" is the class instance with user-defined properties
        if (!obj) return false
        var str = obj.toString();
        return str.startsWith(className + "(") || str.startsWith(className + "_QML");
    }

    function navigateTo(url, objectType)
    {
        var existingItem = stackView.find(function(item, index) {
            return qmltypeof(item, objectType)
        })

        if (existingItem !== null) {
            // Pop to the existing item
            stackView.pop(existingItem)
        }
        else {
            // Create a new item
            stackView.push(url)
        }
    }

    Connections {
        target: peerManager
        function onPeerBound(peer) { ComputerManager.addBoundHost(peer) }
    }
    BindingApproval {
        manager: peerManager
        appWindow: window
    }

    Rectangle {
        id: navigation
        visible: navigationVisible
        width: window.width < 900 ? 174 : 208
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        color: ui.surface
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: ui.line }
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 18; spacing: 8
            RowLayout {
                Layout.topMargin: 12; Layout.bottomMargin: 30
                Image { source: "qrc:/res/deskport.svg"; Layout.preferredWidth: 30; Layout.preferredHeight: 30 }
                Label { text: "DeskPort"; font.pixelSize: 22; font.weight: Font.DemiBold; color: ui.text }
            }
            Repeater {
                model: [qsTr("Devices"), qsTr("Sharing"), qsTr("Settings")]
                Button {
                    Layout.fillWidth: true; implicitHeight: 46
                    text: modelData; flat: true
                    highlighted: index === 0 ? qmltypeof(stackView.currentItem, "PcView") || qmltypeof(stackView.currentItem, "AppView") || qmltypeof(stackView.currentItem, "BindView") : index === 1 ? qmltypeof(stackView.currentItem, "HostView") : qmltypeof(stackView.currentItem, "SettingsHome") || qmltypeof(stackView.currentItem, "SettingsView")
                    background: Rectangle { radius: 9; color: parent.highlighted ? ui.raised : parent.hovered ? "#252b2f" : "transparent"; border.color: parent.highlighted ? ui.line : "transparent" }
                    onClicked: {
                        showDevices()
                        if (index === 1) navigateTo("qrc:/gui/HostView.qml", "HostView")
                        if (index === 2) navigateTo("qrc:/gui/SettingsHome.qml", "SettingsHome")
                    }
                }
            }
            Item { Layout.fillHeight: true }
            Button { text: qsTr("Getting started"); flat: true; Layout.fillWidth: true; onClicked: navigateTo("qrc:/gui/SetupView.qml", "SetupView") }
            Rectangle { Layout.fillWidth: true; height: 1; color: ui.line }
            Label { text: hostManager.deviceName; textFormat: Text.PlainText; color: ui.text; Layout.fillWidth: true; elide: Text.ElideRight; Layout.topMargin: 12 }
            Label { text: hostManager.running ? qsTr("Sharing on") : qsTr("Sharing off"); color: hostManager.running ? ui.accent : ui.muted; font.pixelSize: 12 }
            Label { text: "DeskPort " + SystemProperties.versionString; color: ui.muted; font.pixelSize: 11; Layout.bottomMargin: 6 }
        }
    }
    Rectangle {
        visible: navigationVisible
        anchors.left: navigation.right; anchors.right: parent.right; anchors.top: parent.top; height: 66
        color: ui.canvas
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: ui.line }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 12
            Button { visible: stackView.depth > 1; text: "←"; Accessible.name: qsTr("Back"); flat: true; onClicked: goBack() }
            Label { text: stackView.currentItem ? stackView.currentItem.objectName : "DeskPort"; color: ui.text; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
            UiButton { text: qsTr("Add a device"); visible: qmltypeof(stackView.currentItem, "PcView"); highlighted: true; onClicked: navigateTo("qrc:/gui/BindView.qml", "BindView") }
        }
    }
    Shortcut { enabled: navigationVisible; sequences: [StandardKey.New]; onActivated: navigateTo("qrc:/gui/BindView.qml", "BindView") }
    Shortcut { enabled: navigationVisible; sequences: [StandardKey.Preferences]; onActivated: navigateTo("qrc:/gui/SettingsHome.qml", "SettingsHome") }
    Shortcut { enabled: navigationVisible; sequences: [StandardKey.HelpContents]; onActivated: navigateTo("qrc:/gui/SetupView.qml", "SetupView") }

    ErrorMessageDialog {
        id: noHwDecoderDialog
        text: qsTr("No functioning hardware accelerated video decoder was detected by Moonlight. " +
                   "Your streaming performance may be severely degraded in this configuration.")
        helpText: qsTr("Click the Help button for more information on solving this problem.")
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Fixing-Hardware-Decoding-Problems"
    }

    ErrorMessageDialog {
        id: xWaylandDialog
        text: qsTr("Hardware acceleration doesn't work on XWayland. Continuing on XWayland may result in poor streaming performance. " +
                   "Try running with QT_QPA_PLATFORM=wayland or switch to X11.")
        helpText: qsTr("Click the Help button for more information.")
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Fixing-Hardware-Decoding-Problems"
    }

    NavigableMessageDialog {
        id: wow64Dialog
        standardButtons: Dialog.Ok | Dialog.Cancel
        text: qsTr("This version of Moonlight isn't optimized for your PC. Please download the '%1' version of Moonlight for the best streaming performance.").arg(SystemProperties.friendlyNativeArchName)
        onAccepted: {
            Qt.openUrlExternally("https://github.com/keithxc/deskport/releases");
        }
    }

    ErrorMessageDialog {
        id: unmappedGamepadDialog
        property string unmappedGamepads : ""
        text: qsTr("Moonlight detected gamepads without a mapping:") + "\n" + unmappedGamepads
        helpTextSeparator: "\n\n"
        helpText: qsTr("Click the Help button for information on how to map your gamepads.")
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Gamepad-Mapping"
    }

    // This dialog appears when quitting via keyboard or gamepad button
    NavigableMessageDialog {
        id: quitConfirmationDialog
        standardButtons: Dialog.Yes | Dialog.No
        text: qsTr("Are you sure you want to quit?")
        // For keyboard/gamepad navigation
        onAccepted: window.hide()
    }

    // HACK: This belongs in StreamSegue but keeping a dialog around after the parent
    // dies can trigger bugs in Qt 5.12 that cause the app to crash. For now, we will
    // host this dialog in a QML component that is never destroyed.
    //
    // To repro: Start a stream, cut the network connection to trigger the "Connection
    // terminated" dialog, wait until the app grid times out back to the PC grid, then
    // try to dismiss the dialog.
    ErrorMessageDialog {
        id: streamSegueErrorDialog

        property bool quitAfter: false

        onClosed: {
            if (quitAfter) {
                Qt.quit()
            }

            // StreamSegue assumes its dialog will be re-created each time we
            // start streaming, so fake it by wiping out the text each time.
            text = ""
        }
    }

    NavigableDialog {
        id: addPcDialog
        property string label: qsTr("Enter the host IP address or hostname:")

        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: {
            // Force keyboard focus on the textbox so keyboard navigation works
            editText.forceActiveFocus()
        }

        onClosed: {
            editText.clear()
        }

        onAccepted: {
            if (editText.text) {
                ComputerManager.addNewHostManually(editText.text.trim())
            }
        }

        ColumnLayout {
            Label {
                text: addPcDialog.label
                font.bold: true
            }

            Label {
                text: qsTr("DeskPort defaults to :48989. If the sharing page shows another port, enter address:port. For a default Sunshine host, use :47989.")
                Layout.preferredWidth: 420
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            TextField {
                id: editText
                Layout.fillWidth: true
                focus: true

                Keys.onReturnPressed: {
                    addPcDialog.accept()
                }

                Keys.onEnterPressed: {
                    addPcDialog.accept()
                }
            }
        }
    }
}
