import QtQuick

Window {
    id: root

    required property var pages

    width: 1280
    height: 800
    visible: true
    title: "VDK Sorting " + Qt.application.version

    NavBar {
        id: nav
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        model: root.pages
    }

    Loader {
        anchors { left: nav.right; right: parent.right; top: parent.top; bottom: parent.bottom }
        active: root.pages.count > 0
        source: { root.pages.count; return root.pages.sourceAt(nav.currentIndex) }
    }

    Text {
        anchors { left: nav.right; right: parent.right; verticalCenter: parent.verticalCenter }
        horizontalAlignment: Text.AlignHCenter
        visible: root.pages.count === 0
        text: "No pages loaded"
        font.pixelSize: 24
    }
}