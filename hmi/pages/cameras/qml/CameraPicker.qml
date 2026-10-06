import QtQuick
import VsortHmi

// One button per camera. `cameraId` follows the selected button (-1 while there are no cameras).
Row {
    id: picker

    required property var live // LiveViewModel
    property int cameraIndex: 0 // row of the selected camera
    property int cameraId: -1

    spacing: Theme.spacing

    Connections {
        target: picker.live
        function onModelReset() {
            picker.cameraIndex = 0;
        }
    }

    Repeater {
        model: picker.live
        delegate: VsButton {
            id: camButton

            required property int index
            required property int cameraId

            objectName: "cameraButton"
            text: "Camera " + camButton.cameraId
            primary: camButton.index === picker.cameraIndex
            onClicked: picker.cameraIndex = camButton.index

            Binding {
                target: picker
                property: "cameraId"
                value: camButton.cameraId
                when: camButton.index === picker.cameraIndex
            }
        }
    }
}
