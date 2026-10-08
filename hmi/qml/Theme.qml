pragma Singleton
import QtQuick

QtObject {
    // Colours (dark industrial theme)
    readonly property color background: "#14181d"
    readonly property color surface: "#1d232a"
    readonly property color surfaceRaised: "#262e37"
    readonly property color border: "#3a4552"
    readonly property color textPrimary: "#e8edf2"
    readonly property color textSecondary: "#9aa7b5"
    readonly property color textDisabled: "#5d6977"
    readonly property color accent: "#2f81f7"
    readonly property color accentPressed: "#1c5fc4"
    readonly property color textOnAccent: "#ffffff"
    readonly property color ok: "#3fb950"
    readonly property color warn: "#d29922"
    readonly property color error: "#f85149"

    // Typography (pixel sizes)
    readonly property string fontFamily: Qt.application.font.family
    readonly property int sizeCaption: 14
    readonly property int sizeBody: 18
    readonly property int sizeTitle: 24
    readonly property int sizeHeading: 32

    // Metrics
    readonly property int spacing: 8
    readonly property int padding: 16
    readonly property int radius: 6
    readonly property int controlHeight: 44
    readonly property int tableRowHeight: 28 // dense data tables (Product Monitor)
    readonly property color rowFilled: "#4B5563" // Product Monitor: cup with product
    readonly property int borderWidth: 1
}
