import QtQuick

Rectangle {
    id: bar

    required property var model
    property int currentIndex: 0

    implicitWidth: 140
    color: "#1e2530"

    ListView {
        anchors.fill: parent
        model: bar.model
        interactive: false

        delegate: Rectangle {
            id: item

            required property int index
            required property string pageId
            required property string title

            width: ListView.view.width
            height: 72
            color: item.index === bar.currentIndex ? "#3b82f6" : "transparent"

            Column {
                anchors.centerIn: parent
                spacing: 2

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: item.pageId
                    color: "#9ca3af"
                    font.pixelSize: 12
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: item.title
                    color: "white"
                    font.pixelSize: 18
                }
            }

            TapHandler {
                onTapped: bar.currentIndex = item.index
            }
        }
    }
}
