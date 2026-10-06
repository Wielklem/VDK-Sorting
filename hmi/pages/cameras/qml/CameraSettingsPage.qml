import QtQuick
import VsortHmi

// G30.30 Camera settings (P30.80): exposure and gain of one camera, applied to the camera at once.
Rectangle {
    id: page

    required property var live // LiveViewModel
    required property var settings // CameraSettingsModel

    readonly property int cameraId: picker.cameraId

    color: Theme.background

    onCameraIdChanged: {
        video.clear();
        page.live.attachVideo(page.cameraId, video); // attaching again moves the video
        page.settings.load(page.cameraId);
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
            id: title

            anchors {
                left: parent.left
                leftMargin: Theme.padding
                verticalCenter: parent.verticalCenter
            }
            text: "Camera settings"
            variant: VsText.Title
        }

        CameraPicker {
            id: picker

            live: page.live
            anchors {
                left: title.right
                leftMargin: Theme.padding
                verticalCenter: parent.verticalCenter
            }
        }
    }

    Column {
        id: form

        anchors {
            left: parent.left
            top: bar.bottom
            margins: Theme.padding
        }
        width: 360
        spacing: Theme.spacing

        VsText {
            text: "Exposure"
            variant: VsText.Caption
        }
        VsNumberInput {
            id: exposureInput

            objectName: "exposureInput"
            width: parent.width
            from: 10
            to: 1000000
            decimals: 0
            unit: "µs"
            enabled: page.settings.loaded && !page.settings.busy

            Binding {
                target: exposureInput
                property: "value"
                value: page.settings.exposureUs
            }
        }

        VsText {
            text: "Gain"
            variant: VsText.Caption
        }
        VsNumberInput {
            id: gainInput

            objectName: "gainInput"
            width: parent.width
            from: 0
            to: 40
            decimals: 1
            unit: "dB"
            enabled: page.settings.loaded && !page.settings.busy

            Binding {
                target: gainInput
                property: "value"
                value: page.settings.gainDb
            }
        }

        VsButton {
            objectName: "applyButton"
            text: "Apply"
            primary: true
            enabled: page.settings.loaded && !page.settings.busy
            onClicked: page.settings.apply(exposureInput.value, gainInput.value)
        }

        VsText {
            objectName: "statusText"
            width: parent.width
            variant: VsText.Caption
            color: Theme.warn
            text: page.settings.status
        }
    }

    Item {
        anchors {
            left: form.right
            leftMargin: Theme.padding
            right: parent.right
            top: bar.bottom
            bottom: parent.bottom
        }

        VideoItem {
            id: video

            objectName: "video"
            anchors.fill: parent
        }

        VsText {
            anchors.centerIn: parent
            visible: !video.hasFrame
            color: Theme.textSecondary
            text: page.live.connected ? "Waiting for frames…" : "Service offline"
        }
    }
}
