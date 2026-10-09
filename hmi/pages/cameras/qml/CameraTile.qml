import QtQuick
import VsortHmi

// One camera: picture, status line and a freeze button. Used in the grid and in fullscreen.
Rectangle {
    id: tile

    property var live // LiveViewModel, gets the frames to the VideoItem below

    required property int cameraId
    required property string serial
    required property string linkState
    required property bool frozen
    required property bool hasFrame
    required property int frameWidth
    required property int frameHeight
    required property real incomingFps // service: frames in; -1 = not known
    required property real analysedFps // service: frames analysed without error; -1 = not known

    signal activated
    signal freezeToggled

    color: Theme.background
    border.width: Theme.borderWidth
    border.color: tile.frozen ? Theme.warn : Theme.border

    VideoItem {
        id: video

        objectName: "video"
        anchors.fill: parent
        anchors.margins: Theme.borderWidth

        // The model pushes this camera's frames into the item until it is destroyed.
        Component.onCompleted: tile.live.attachVideo(tile.cameraId, video)
    }

    VsText {
        anchors.centerIn: parent
        visible: !tile.hasFrame
        color: Theme.textSecondary
        text: tile.linkState === "offline" ? "Service offline" : "Waiting for frames…"
    }

    MouseArea {
        anchors.fill: parent
        onClicked: tile.activated()
    }

    Rectangle {
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        height: statusLeft.implicitHeight + Theme.spacing
        color: "#b3000000"

        VsText {
            id: statusLeft

            anchors {
                left: parent.left
                leftMargin: Theme.spacing
                verticalCenter: parent.verticalCenter
            }
            variant: VsText.Caption
            color: Theme.textPrimary
            text: "Camera " + tile.cameraId + "  " + tile.serial + "  [" + tile.linkState + "]"
        }
        VsText {
            anchors {
                right: parent.right
                rightMargin: Theme.spacing
                verticalCenter: parent.verticalCenter
            }
            variant: VsText.Caption
            // Preview size (downscaled), then the camera's real rates (P30.86).
            text: tile.hasFrame ? tile.frameWidth + "×" + tile.frameHeight + "  " + (tile.incomingFps < 0 ? "–" : tile.incomingFps.toFixed(1)) + " fps in, " + (tile.analysedFps < 0 ? "–" : tile.analysedFps.toFixed(1)) + " analysed" : ""
        }
    }

    Rectangle {
        objectName: "frozenBadge"
        visible: tile.frozen
        anchors.centerIn: parent
        width: badgeText.implicitWidth + 2 * Theme.padding
        height: Theme.controlHeight
        radius: Theme.radius
        color: Theme.warn

        VsText {
            id: badgeText

            anchors.centerIn: parent
            text: "FROZEN"
            variant: VsText.Title
            color: Theme.background
        }
    }

    VsButton {
        objectName: "freezeButton"
        anchors {
            right: parent.right
            bottom: parent.bottom
            margins: Theme.spacing
        }
        text: tile.frozen ? "Unfreeze" : "Freeze"
        onClicked: tile.freezeToggled()
    }
}
