import QtQuick
import VsortHmi

// G60 Analytics: tabs (only G60.20 Parameters so far, P60.45; P60.90 adds the stage list and the
// debug overlays). `liveView` and `analysisTuning` are the models that main.cpp puts into the QML
// context.
Item {
    id: root

    Rectangle {
        id: tabs

        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        height: Theme.controlHeight + 2 * Theme.spacing
        color: Theme.surfaceRaised

        Row {
            anchors {
                left: parent.left
                leftMargin: Theme.padding
                verticalCenter: parent.verticalCenter
            }
            spacing: Theme.spacing

            VsButton {
                objectName: "tabParameters"
                text: "Parameters"
                primary: true
            }
        }
    }

    HsvEditorView {
        anchors {
            left: parent.left
            right: parent.right
            top: tabs.bottom
            bottom: parent.bottom
        }
        live: liveView
        tuning: analysisTuning
    }
}
