import QtQuick

// Flat side-navigation row: soft accent fill and accent bar when selected, light fill on hover.
Rectangle {
    id: tab

    property string text
    property bool selected: false
    signal clicked

    implicitHeight: Theme.controlHeight
    radius: Theme.radius
    color: tab.selected ? Theme.accentSoft : (hover.hovered ? Theme.hover : "transparent")

    Rectangle {
        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
            topMargin: Theme.spacing
            bottomMargin: Theme.spacing
        }
        width: Theme.indicatorWidth
        radius: width / 2
        color: Theme.accent
        visible: tab.selected
    }

    Text {
        anchors {
            left: parent.left
            right: parent.right
            leftMargin: Theme.padding
            rightMargin: Theme.spacing
            verticalCenter: parent.verticalCenter
        }
        text: tab.text
        elide: Text.ElideRight
        color: tab.selected ? Theme.textPrimary : Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.sizeBody
        font.weight: tab.selected ? Font.DemiBold : Font.Normal
    }

    HoverHandler {
        id: hover

        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        onTapped: tab.clicked()
    }
}
