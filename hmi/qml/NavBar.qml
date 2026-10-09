import QtQuick

// App header: product name, one tab per page plugin (sorted by order()), version on the right.
Rectangle {
    id: bar

    required property var model
    property int currentIndex: 0

    implicitHeight: Theme.headerHeight
    color: Theme.surface

    VsText {
        id: brand

        anchors {
            left: parent.left
            leftMargin: Theme.padding
            verticalCenter: parent.verticalCenter
        }
        text: "VDK Sorting"
        variant: VsText.Title
    }

    Rectangle {
        id: divider

        anchors {
            left: brand.right
            leftMargin: Theme.padding
            verticalCenter: parent.verticalCenter
        }
        width: Theme.borderWidth
        height: parent.height / 2
        color: Theme.border
    }

    ListView {
        anchors {
            left: divider.right
            leftMargin: Theme.spacing
            right: version.left
            top: parent.top
            bottom: parent.bottom
        }
        orientation: ListView.Horizontal
        model: bar.model
        interactive: false
        clip: true

        delegate: Item {
            id: item

            required property int index
            required property string pageId
            required property string title
            readonly property bool current: item.index === bar.currentIndex

            objectName: "navItem"
            width: label.implicitWidth + 2 * Theme.padding
            height: ListView.view.height

            Rectangle {
                anchors {
                    fill: parent
                    topMargin: Theme.spacing
                    bottomMargin: Theme.spacing
                }
                radius: Theme.radius
                color: Theme.hover
                visible: hover.hovered && !item.current
            }

            Row {
                id: label

                anchors.centerIn: parent
                spacing: Theme.spacing

                Text {
                    anchors.baseline: name.baseline
                    text: item.pageId
                    color: item.current ? Theme.accent : Theme.textDisabled
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.sizeCaption
                }
                Text {
                    id: name

                    text: item.title
                    color: item.current ? Theme.textPrimary : Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.sizeBody
                    font.weight: item.current ? Font.DemiBold : Font.Normal
                }
            }

            Rectangle {
                anchors {
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                    leftMargin: Theme.spacing
                    rightMargin: Theme.spacing
                }
                height: Theme.indicatorWidth
                radius: height / 2
                color: Theme.accent
                visible: item.current
            }

            HoverHandler {
                id: hover

                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                onTapped: bar.currentIndex = item.index
            }
        }
    }

    VsText {
        id: version

        anchors {
            right: parent.right
            rightMargin: Theme.padding
            verticalCenter: parent.verticalCenter
        }
        text: "v" + Qt.application.version
        variant: VsText.Caption
    }

    Rectangle {
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: Theme.borderWidth
        color: Theme.border
    }
}
