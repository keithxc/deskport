import QtQuick 2.9
QtObject {
    property int mode: 0
    property SystemPalette systemPalette: SystemPalette { colorGroup: SystemPalette.Active }
    readonly property bool dark: mode === 2 || (mode === 0 && systemPalette.window.hslLightness < 0.5)
    readonly property color canvas: dark ? "#141719" : "#f4f7f6"
    readonly property color surface: dark ? "#1e2326" : "#ffffff"
    readonly property color raised: dark ? "#272e32" : "#e8efec"
    readonly property color line: dark ? "#343d42" : "#cdd9d3"
    readonly property color text: dark ? "#f1f4f3" : "#1c3027"
    readonly property color muted: dark ? "#a4b1b5" : "#52685d"
    readonly property color accent: dark ? "#a6e3c7" : "#216b4b"
    readonly property color accentText: dark ? "#14271e" : "#ffffff"
    readonly property color warning: dark ? "#f0c987" : "#855300"
    readonly property int small: 12
    readonly property int body: 14
    readonly property int title: 20
    readonly property int heading: 26
    readonly property int gap: 12
    readonly property int padding: 20
    readonly property int radius: 12
}
