import QtQuick

Window {
    id: root

    required property var pages

    width: 1280
    height: 800
    visible: true
    title: "VDK Sorting " + Qt.application.version
    color: Theme.background

    NavBar {
        id: nav
        anchors { left: parent.left; right: parent.right; top: parent.top }
        model: root.pages
    }

    Loader {
        anchors { left: parent.left; right: parent.right; top: nav.bottom; bottom: parent.bottom }
        active: root.pages.count > 0
        source: { root.pages.count; return root.pages.sourceAt(nav.currentIndex) }
    }

    Text {
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
        horizontalAlignment: Text.AlignHCenter
        visible: root.pages.count === 0
        text: "No pages loaded"
        font.pixelSize: 24
    }
}