import QtQuick
import VsortHmi

// G35 Calibrate: one camera live with the overlay layer on top (P30.70).
// rois / lanes / detections stay empty until the config (P30.80) and tracking (P40) fill them.
// "Demo data" draws sample shapes so the layer can be checked on a real picture.
Rectangle {
    id: page

    required property var live // LiveViewModel
    property int cameraIndex: 0 // row of the selected camera
    property int cameraId: -1 // set by the selected camera button
    property var rois: []
    property var lanes: []
    property var detections: []

    readonly property real frameW: video.sourceSize.width
    readonly property real frameH: video.sourceSize.height
    readonly property var demoRois: [0, 1, 2].map(i => ({
                x: frameW * (0.05 + 0.3 * i),
                y: frameH * 0.2,
                width: frameW * 0.25,
                height: frameH * 0.6,
                label: "Lane " + (i + 1)
            }))
    readonly property var demoLanes: [0, 1, 2, 3].map(i => ({
                x1: frameW * (0.025 + 0.3 * i),
                y1: 0,
                x2: frameW * (0.025 + 0.3 * i),
                y2: frameH
            }))
    readonly property var demoDetections: [0, 1, 2].map(i => ({
                x: frameW * (0.125 + 0.3 * i),
                y: frameH * 0.45,
                width: frameW * 0.1,
                height: frameH * 0.14,
                label: "#" + (i + 1)
            }))

    color: Theme.background

    onCameraIdChanged: {
        video.clear();
        page.live.attachVideo(page.cameraId, video); // attaching again moves the video
    }

    Connections {
        target: page.live
        function onModelReset() {
            page.cameraIndex = 0;
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

        Row {
            anchors {
                left: parent.left
                leftMargin: Theme.padding
                verticalCenter: parent.verticalCenter
            }
            spacing: Theme.spacing

            VsText {
                anchors.verticalCenter: parent.verticalCenter
                text: "Calibrate"
                variant: VsText.Title
            }

            Repeater {
                model: page.live
                delegate: VsButton {
                    id: camButton

                    required property int index
                    required property int cameraId

                    objectName: "cameraButton"
                    text: "Camera " + cameraId
                    primary: index === page.cameraIndex
                    onClicked: page.cameraIndex = index

                    Binding {
                        target: page
                        property: "cameraId"
                        value: camButton.cameraId
                        when: camButton.index === page.cameraIndex
                    }
                }
            }
        }

        Row {
            anchors {
                right: parent.right
                rightMargin: Theme.padding
                verticalCenter: parent.verticalCenter
            }
            spacing: Theme.spacing

            VsButton {
                id: roiToggle

                objectName: "roiToggle"
                checkable: true
                checked: true
                primary: checked
                text: "ROIs"
            }
            VsButton {
                id: laneToggle

                objectName: "laneToggle"
                checkable: true
                checked: true
                primary: checked
                text: "Lanes"
            }
            VsButton {
                id: detToggle

                objectName: "detToggle"
                checkable: true
                checked: true
                primary: checked
                text: "Detections"
            }
            VsButton {
                id: demoToggle

                objectName: "demoToggle"
                checkable: true
                primary: checked
                text: "Demo data"
            }
        }
    }

    Item {
        anchors {
            left: parent.left
            right: parent.right
            top: bar.bottom
            bottom: parent.bottom
        }

        VideoItem {
            id: video

            objectName: "video"
            anchors.fill: parent
        }

        OverlayLayer {
            id: overlay

            objectName: "overlay"
            anchors.fill: parent
            sourceSize: video.sourceSize
            rois: demoToggle.checked ? page.demoRois : page.rois
            lanes: demoToggle.checked ? page.demoLanes : page.lanes
            detections: demoToggle.checked ? page.demoDetections : page.detections
            showRois: roiToggle.checked
            showLanes: laneToggle.checked
            showDetections: detToggle.checked
        }

        VsText {
            anchors.centerIn: parent
            visible: !video.hasFrame
            color: Theme.textSecondary
            text: page.live.connected ? "Waiting for frames…" : "Service offline"
        }
    }
}
