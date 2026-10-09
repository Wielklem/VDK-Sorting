pragma Singleton
import QtQuick

QtObject {
    // Colours (dark industrial theme)
    readonly property color background: "#14181d"
    readonly property color surface: "#1d232a"
    readonly property color surfaceRaised: "#262e37"
    readonly property color header: "#323c48" // app header, brighter than the side navigation
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
    readonly property int sideNavWidth: 200 // in-page tab column (left)
    readonly property int headerHeight: 140 // app header with page tabs
    readonly property int tabMinWidth: 300 // minimum page tab width
    readonly property int tabPadding: 30 // horizontal padding inside a page tab
    readonly property int navBrandSize: 18 // "Logo Text" in the header
    readonly property int navTitleSize: 26 // page tab title
    readonly property int navIdSize: 18 // page tab ID (G30, G140)
    readonly property int indicatorWidth: 3 // accent bar of the selected tab
    readonly property color accentSoft: "#262f81f7" // accent at 15 %: selected side tab
    readonly property color accentTint: "#402f81f7" // accent at 25 %: selected page tab
    readonly property color hover: "#14ffffff" // white at 8 %: hover on flat items
    readonly property int tableRowHeight: 28 // dense data tables (Product Monitor)
    readonly property color rowFilled: "#4B5563" // Product Monitor: cup with product
    readonly property int borderWidth: 1
}
