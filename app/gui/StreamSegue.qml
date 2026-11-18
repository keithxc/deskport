import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Window 2.2
import QtQuick.Layouts 1.3

import SdlGamepadKeyNavigation 1.0
import Session 1.0

Item {
    id: streamPage
    // Isolated harnesses run without the window theme.
    readonly property var theme: typeof ui !== "undefined" ? ui : fallbackTheme
    UiTheme { id: fallbackTheme }
    readonly property bool hidesNavigation: true
    property Session session
    property string appName
    property string stageText : isResume ? qsTr("Resuming %1...").arg(appName) :
                                           qsTr("Starting %1...").arg(appName)
    property bool isResume : false
    property bool quitAfter : false

    function stageStarting(stage)
    {
        // Update the spinner text
        stageText = qsTr("Starting %1...").arg(stage)
    }

    function stageFailed(stage, errorCode, failingPorts)
    {
        // Display the error dialog after Session::exec() returns
        streamSegueErrorDialog.text = qsTr("Starting %1 failed: Error %2").arg(stage).arg(errorCode)

        if (failingPorts) {
            streamSegueErrorDialog.text += "\n\n" + qsTr("Check your firewall and port forwarding rules for port(s): %1").arg(failingPorts)
        }
    }

    function connectionStarted()
    {
        streamSegueErrorDialog.text = ""
        stageText = qsTr("Waiting for desktop video…")
    }

    function viewerReadyChanged()
    {
        if (typeof session === "undefined" || !session) return
        if (session.viewerReady && stackView.currentItem === streamPage)
            window.visible = false
    }

    function displayLaunchError(text)
    {
        // Display the error dialog after Session::exec() returns
        streamSegueErrorDialog.text = text
        console.error(text)
    }

    function displayLaunchWarning(text)
    {
        if (session && session.quietRecovery()) return;
        // Toast lifetime is independent of connection progress.
        var toast = Qt.createQmlObject('import QtQuick.Controls 2.2; ToolTip {}', parent, '')
        toast.text = text
        toast.timeout = 3000
        toast.visible = true
        console.warn(text)
    }

    function quitStarting()
    {
        // Avoid the push transition animation. Let StackView create the page
        // in its own context; this page's context is destroyed by replace().
        stackView.replace(stackView.currentItem, Qt.resolvedUrl("QuitSegue.qml"), {"appName": appName}, StackView.Immediate)

        // Show the Qt window again to show quit segue
        window.visible = true
    }

    function sessionFinished(portTestResult)
    {
        if (typeof session === 'undefined' || !session) return
        if (session.adaptiveRestartPending()) return
        var quiet = session.quietRecovery()
        if (portTestResult !== 0 && portTestResult !== -1 && streamSegueErrorDialog.text) {
            streamSegueErrorDialog.text += "\n\n" + qsTr("This PC's Internet connection is blocking Moonlight. Streaming over the Internet may not work while connected to this network.")
        }

        // Enable GUI gamepad usage now
        SdlGamepadKeyNavigation.enable()

        if (quitAfter) {
            if (quiet) { Qt.quit(); return }
            if (streamSegueErrorDialog.text) {
                // Quit when the error dialog is acknowledged
                streamSegueErrorDialog.quitAfter = quitAfter
                streamSegueErrorDialog.open()
            }
            else {
                // Quit immediately
                Qt.quit()
            }
        } else {
            // Exit this view
            // The control center may be stacked above this retained session.
            // Remove it first, then leave the stream page and return to Devices.
            if (stackView.currentItem !== streamPage) stackView.pop(streamPage, StackView.Immediate)
            stackView.pop(StackView.Immediate)

            // Lifecycle recovery and its failures must not summon the shell.
            if (!quiet) window.visible = true

            // Display any launch errors. We do this after
            // the Qt UI is visible again to prevent losing
            // focus on the dialog which would impact gamepad
            // users.
            if (streamSegueErrorDialog.text && !quiet) {
                streamSegueErrorDialog.quitAfter = quitAfter
                streamSegueErrorDialog.open()
            }
        }
    }

    function sessionReadyForDeletion()
    {
        // sessionFinished() may already have popped and destroyed this page's
        // bindings; the Session then cleans itself up without our help. A
        // destroyed context takes its properties with it, so `session` is not
        // merely null here: naming it at all throws a ReferenceError.
        if (typeof session === 'undefined' || !session) return
        if (session.adaptiveRestartPending()) {
            var previous = session
            var next = previous.adaptiveContinuation()
            // Admission can be revoked between the pending check and cloning.
            if (!next) { sessionFinished(0); return }
            // Keep the navigation stack intact: Devices may be above this page.
            // Transport replacement must not dismiss it or steal window focus.
            previous.stageStarting.disconnect(stageStarting)
            previous.stageFailed.disconnect(stageFailed)
            previous.connectionStarted.disconnect(connectionStarted)
            previous.viewerReadyChanged.disconnect(viewerReadyChanged)
            previous.displayLaunchError.disconnect(displayLaunchError)
            previous.displayLaunchWarning.disconnect(displayLaunchWarning)
            previous.quitStarting.disconnect(quitStarting)
            previous.sessionFinished.disconnect(sessionFinished)
            previous.readyForDeletion.disconnect(sessionReadyForDeletion)
            streamLoader.active = false
            retryTimer.stop()
            session = next
            isResume = true
            sessionHooked = false
            startSession()
            return
        }
        // Drop the page reference. C++ releases the Session after both exec()
        // and asynchronous transport cleanup have finished.
        session = null
        gc()
    }

    property bool sessionHooked: false

    StackView.onActivated: startSession()

    function startSession() {
        // The control center can be pushed above this page and popped again.
        // Reconnecting would deliver every session signal several times.
        if (sessionHooked) return
        sessionHooked = true

        // Hook up our signals
        session.stageStarting.connect(stageStarting)
        session.stageFailed.connect(stageFailed)
        session.connectionStarted.connect(connectionStarted)
        session.viewerReadyChanged.connect(viewerReadyChanged)
        session.displayLaunchError.connect(displayLaunchError)
        session.displayLaunchWarning.connect(displayLaunchWarning)
        session.quitStarting.connect(quitStarting)
        session.sessionFinished.connect(sessionFinished)
        session.readyForDeletion.connect(sessionReadyForDeletion)

        // Kick off the stream
        spinnerTimer.start()
        if (session.retryDelay() > 0) {
            streamSegueErrorDialog.text = ""
            stageText = qsTr("Connection interrupted. Reconnecting…")
            if (!session.quietRecovery() && stackView.currentItem === streamPage) window.visible = true
            retryTimer.interval = session.retryDelay()
            retryTimer.start()
        } else streamLoader.active = true
    }

    Timer {
        id: retryTimer
        onTriggered: streamLoader.active = true
    }
    Timer {
        id: spinnerTimer

        // Display the spinner appearance a bit to allow us to reach
        // the code in Session.exec() that pumps the event loop.
        // If we display it immediately, it will briefly hang in the
        // middle of the animation on Windows, which looks very
        // obviously broken.
        interval: 100
        onTriggered: stageSpinner.running = true
    }

    Loader {
        id: streamLoader
        active: false
        asynchronous: true

        onLoaded: {
            // Set the hint text. We do this here rather than
            // in the hintText control itself to synchronize
            // with Session.exec() which requires no concurrent
            // gamepad usage.
            hintText.text = qsTr("Tip:") + " " + qsTr("Press %1 to leave fullscreen; press again to disconnect").arg(SdlGamepadKeyNavigation.getConnectedGamepads() > 0 ?
                                                  qsTr("Start+Select+L1+R1") : qsTr("Ctrl+Alt+Shift+Q"))

            // Stop GUI gamepad usage now
            SdlGamepadKeyNavigation.disable()

            // Garbage collect QML stuff before we start streaming,
            // since we'll probably be streaming for a while and we
            // won't be able to GC during the stream.
            gc()

            // Run the streaming session to completion
            // Finish Loader incubation before running the session. A transport
            // continuation can deactivate this Loader during a nested event loop.
            Qt.callLater(function() {
                // StackView pages can be detached from the visual window
                // during deferred loading. Use the root window context,
                // as the other session lifecycle callbacks do.
                if (session) {
                    // Devices may have activated since Loader.onLoaded.
                    // Hand off SDL immediately before acquiring session ownership.
                    SdlGamepadKeyNavigation.disable()
                    session.exec(window)
                }
            })
        }

        sourceComponent: Item {}
    }

    // The wait is a centred panel with its cancel action.
    Rectangle {
        objectName: "streamWait"
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 460)
        height: waitColumn.implicitHeight + 48
        radius: 20
        color: streamPage.theme.surface
        border.color: streamPage.theme.line
        ColumnLayout {
            id: waitColumn
            x: 24; y: 24; width: parent.width - 48
            spacing: 16

            BusyIndicator {
                id: stageSpinner
                Layout.alignment: Qt.AlignHCenter
                running: false
            }

            Label {
                id: stageLabel
                Layout.fillWidth: true
                text: stageText
                color: streamPage.theme.text
                font.pixelSize: 18
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }

            UiButton {
                Layout.alignment: Qt.AlignHCenter
                visible: session && !session.viewerReady
                objectName: "cancelReconnect"
                text: session && session.retryDelay() > 0 ? qsTr("Cancel reconnect") : qsTr("Cancel connection")
                onClicked: {
                    streamSegueErrorDialog.text = ""
                    session.cancelRecovery()
                    retryTimer.stop()
                    // Run cancellation through Session's normal lifetime barrier.
                    if (!streamLoader.active) streamLoader.active = true
                }
            }
        }
    }

    Label {
        id: hintText
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 50
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(implicitWidth, parent.width - 32)
        color: streamPage.theme.muted
        font.pixelSize: 15
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter

        wrapMode: Text.Wrap
    }
}
