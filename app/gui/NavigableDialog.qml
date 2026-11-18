import QtQuick 2.0
import QtQuick.Controls 2.5

// Every choice, confirmation, message, text entry and wait is this centred,
// rounded panel on a dimmed backdrop: at most 460 wide and 82% of the window
// tall. Clicking outside, Esc, Cancel or the close button closes it. A titled
// panel keeps its title row and close button fixed while its body scrolls.
Dialog {
    id: panel
    readonly property var panelTheme: typeof ui !== "undefined" ? ui : fallbackTheme
    UiTheme { id: fallbackTheme }
    // Shows the accepting button in red; it still needs an explicit click.
    property bool destructive: false
    // Replaces the accepting button's text, e.g. "Remove" instead of "Yes".
    property string acceptText: ""
    property bool showClose: title.length > 0
    property Item focusBodyItem: null
    // Only callers that register the COMPLETE body traversal may opt in.
    // Forms/options use Qt's full chain, including all text fields.
    property bool explicitFocusCycle: false
    // Explicit traversal keeps message actions reachable with macOS text-only Tab.
    function cycleFocus(current, backwards) {
        if (!explicitFocusCycle) return
        var controls = []
        if (focusBodyItem && focusBodyItem.visible && focusBodyItem.enabled) controls.push(focusBodyItem)
        if (closeButton.visible && closeButton.enabled) controls.push(closeButton)
        for (var i = 0; footer && i < footer.count; ++i) {
            var button = footer.itemAt(i)
            if (button && button.visible && button.enabled) controls.push(button)
        }
        if (controls.length === 0) return
        var index = controls.indexOf(current)
        var next = index < 0 ? (backwards ? controls.length - 1 : 0)
                            : (index + (backwards ? -1 : 1) + controls.length) % controls.length
        controls[next].forceActiveFocus(Qt.TabFocusReason)
    }
    readonly property real maximumHeight: Math.round((parent ? parent.height : 640) * 0.82)
    modal: true
    padding: 20
    topPadding: title.length > 0 ? 4 : 20
    width: Math.min(460, (parent ? parent.width : 492) - 32)
    height: Math.min(implicitHeight, maximumHeight)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: panel.panelTheme.scrim }
    background: Rectangle { color: panel.panelTheme.surface; radius: 20; border.color: panel.panelTheme.line }
    header: Item {
        visible: panel.title.length > 0
        implicitHeight: visible ? 58 : 0
        Label {
            anchors.left: parent.left; anchors.leftMargin: 20
            anchors.right: closeButton.visible ? closeButton.left : parent.right; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: panel.title; textFormat: Text.PlainText
            color: panel.panelTheme.text; font.pixelSize: 18; font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        UiIconButton {
            id: closeButton
            objectName: "panelClose"
            visible: panel.showClose
            anchors.right: parent.right; anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            source: "qrc:/res/ui/close.svg"
            Accessible.name: qsTranslate("NavigableDialog", "Close")
            onClicked: panel.reject()
            Keys.onTabPressed: function(event) {
                event.accepted = panel.explicitFocusCycle
                if (event.accepted) panel.cycleFocus(closeButton, false)
            }
            Keys.onBacktabPressed: function(event) {
                event.accepted = panel.explicitFocusCycle
                if (event.accepted) panel.cycleFocus(closeButton, true)
            }
        }
    }
    footer: DialogButtonBox {
        visible: count > 0
        standardButtons: panel.standardButtons
        alignment: Qt.AlignRight
        spacing: 8
        padding: 20; topPadding: 4
        background: Item {}
        delegate: UiButton {
            id: panelButton
            objectName: accepting ? "dialogAcceptButton" : "dialogRejectButton"
            readonly property bool accepting: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
                                              || DialogButtonBox.buttonRole === DialogButtonBox.YesRole
            filled: accepting
            destructive: accepting && panel.destructive
            Keys.onTabPressed: function(event) {
                event.accepted = panel.explicitFocusCycle
                if (event.accepted) panel.cycleFocus(panelButton, false)
            }
            Keys.onBacktabPressed: function(event) {
                event.accepted = panel.explicitFocusCycle
                if (event.accepted) panel.cycleFocus(panelButton, true)
            }
        }
    }

    parent: Overlay.overlay

    x: Math.round(((parent ? parent.width : width) - width) / 2)
    y: Math.round(((parent ? parent.height : height) - height) / 2)

    onAboutToShow: {
        // Qt's standard-button text is assigned when the buttons are created.
        // A retained, unopened panel may predate a live language switch.
        // Refresh the footer only, preserving the caller's standardButtons binding.
        if (footer && footer.standardButtons !== undefined) {
            var buttons = panel.standardButtons
            footer.standardButtons = Dialog.NoButton
            footer.standardButtons = buttons
            footer.standardButtons = Qt.binding(function() { return panel.standardButtons })
        }
        if (acceptText.length === 0 || !standardButton) return
        var button = standardButton(Dialog.Ok) || standardButton(Dialog.Yes)
        if (button) button.text = acceptText
    }

    onAboutToHide: {
        // We must force focus back to the last item for platforms without
        // support for more than one active window like Steam Link. If
        // we don't, gamepad and keyboard navigation will break after a
        // dialog appears.
        if (typeof stackView !== "undefined") stackView.forceActiveFocus()
    }
}
