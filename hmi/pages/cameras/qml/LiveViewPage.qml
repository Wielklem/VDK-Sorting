import QtQuick
import VsortHmi

// G30.10 Live view: grid of all cameras, one camera fullscreen, freeze per camera or for all.
Rectangle {
    id: page

    required property var live // LiveViewModel
    property int focusedId: -1 // -1 = grid, otherwise the camera shown fullscreen

    color: Theme.background
    focus: true
    Keys.onEscapePressed: page.focusedId = -1

    Connections {
        target: page.live
        function onModelReset() {
            page.focusedId = -1;
        }
    }

    Rectangle {
        id: bar

        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        height: Theme.controlHeight + 2 * Theme.spacing
        color: Theme.surface

        VsText {
            anchors {
                left: parent.left
                leftMargin: Theme.padding
                verticalCenter: parent.verticalCenter
            }
            text: "Live view"
            variant: VsText.Title
        }
        VsText {
            objectName: "connectionText"
            anchors.centerIn: parent
            color: page.live.connected ? Theme.ok : Theme.error
            text: page.live.connected ? "Service connected" : "Service offline"
        }
        VsButton {
            objectName: "freezeAllButton"
            anchors {
                right: parent.right
                rightMargin: Theme.padding
                verticalCenter: parent.verticalCenter
            }
            enabled: page.live.cameraCount > 0
            primary: page.live.allFrozen
            text: page.live.allFrozen ? "Unfreeze all" : "Freeze all"
            onClicked: page.live.setAllFrozen(!page.live.allFrozen)
        }
    }

    Item {
        id: area

        readonly property int columns: Math.max(1, Math.ceil(Math.sqrt(page.live.cameraCount)))
        readonly property int rows: Math.max(1, Math.ceil(page.live.cameraCount / columns))

        anchors {
            left: parent.left
            right: parent.right
            top: bar.bottom
            bottom: parent.bottom
        }

        Grid {
            anchors.fill: parent
            columns: area.columns

            Repeater {
                model: page.live
                delegate: CameraTile {
                    live: page.live
                    width: area.width / area.columns
                    height: area.height / area.rows
                    onActivated: page.focusedId = cameraId
                    onFreezeToggled: page.live.toggleFrozen(cameraId)
                }
            }
        }

        // Fullscreen: the same model again, only the focused camera is visible.
        Item {
            id: focusLayer

            objectName: "focusLayer"
            anchors.fill: parent
            visible: page.focusedId >= 0

            Repeater {
                model: page.live
                delegate: CameraTile {
                    live: page.live
                    anchors.fill: parent
                    visible: cameraId === page.focusedId
                    onActivated: page.focusedId = -1 // click or Esc: back to the grid
                    onFreezeToggled: page.live.toggleFrozen(cameraId)
                }
            }
        }
    }
}
