import QtQuick

// App header: product name, one tab per page plugin (sorted by order()), version on the right.
Rectangle {
    id: bar

    required property var model
    property int currentIndex: 0

    implicitHeight: Theme.headerHeight
    color: Theme.header

    VsText {
        id: brand

        anchors {
            left: parent.left
            leftMargin: Theme.padding
            verticalCenter: parent.verticalCenter
        }
        text: "Potatonator"
        variant: VsText.Title
        font.pixelSize: Theme.navBrandSize
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
        spacing: Theme.spacing / 2
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
            width: Math.max(Theme.tabMinWidth, label.implicitWidth + 2 * Theme.tabPadding)
            height: ListView.view.height

            Item { // tab shape: rounded top corners, open at the bottom edge
                anchors {
                    fill: parent
                    topMargin: Theme.spacing
                }
                clip: true

                Rectangle {
                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                    }
                    height: parent.height + radius // bottom corners fall outside the clip
                    radius: Theme.radius
                    color: item.current ? Theme.accentTint : (hover.hovered ? Theme.hover : "transparent")
                }
                Rectangle { // accent line on top of the selected tab
                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        leftMargin: Theme.radius
                        rightMargin: Theme.radius
                    }
                    height: Theme.indicatorWidth
                    radius: height / 2
                    color: Theme.accent
                    visible: item.current
                }
            }

            Row {
                id: label

                anchors {
                    horizontalCenter: parent.horizontalCenter
                    verticalCenter: parent.verticalCenter
                    verticalCenterOffset: Theme.spacing / 2 // centre in the tab, not the header
                }
                spacing: Theme.spacing

                Text {
                    anchors.baseline: name.baseline
                    text: item.pageId
                    color: item.current ? Theme.textPrimary : Theme.textSecondary
                    opacity: item.current ? 0.8 : 1
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.navIdSize
                }
                Text {
                    id: name

                    text: item.title
                    color: item.current ? Theme.textPrimary : Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.navTitleSize
                    font.weight: item.current ? Font.DemiBold : Font.Normal
                }
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
