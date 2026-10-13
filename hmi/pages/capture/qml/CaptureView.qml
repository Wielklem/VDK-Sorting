import QtQuick
import QtQuick.Controls.Basic
import VsortHmi

// G150.10 Photo capture (P60.100): saves photos of the lane ROIs to a folder on the service PC, to
// review how the analysis judged them (are the "empty" cups really empty?). `capture` is the
// CaptureModel (see CapturePage.qml).
Rectangle {
    id: page

    required property var capture // CaptureModel

    readonly property bool locked: page.capture.running || page.capture.busy
    readonly property var modeNames: ["No potatoes", "Single potato", "Multiple potatoes", "Size range"]

    color: Theme.background

    Component.onCompleted: page.capture.refresh()

    Column {
        id: form

        anchors {
            left: parent.left
            top: parent.top
            margins: Theme.padding
        }
        width: 460
        spacing: Theme.spacing

        VsText {
            text: "Photo capture"
            variant: VsText.Title
        }

        VsText {
            text: "Photos per sensor"
            variant: VsText.Caption
        }
        VsNumberInput {
            id: photosInput

            objectName: "photosInput"
            width: parent.width
            from: 1
            to: 100000
            decimals: 0
            enabled: !page.locked
            onEdited: v => page.capture.photosPerSensor = Math.round(v)

            Binding {
                target: photosInput
                property: "value"
                value: page.capture.photosPerSensor
            }
        }

        VsText {
            text: "Save photos with"
            variant: VsText.Caption
        }
        Flow {
            width: parent.width
            spacing: Theme.spacing

            Repeater {
                model: page.modeNames
                delegate: VsButton {
                    required property int index
                    required property string modelData

                    objectName: "mode" + index
                    width: (form.width - Theme.spacing) / 2
                    text: modelData
                    primary: page.capture.mode === index
                    enabled: !page.locked
                    onClicked: page.capture.mode = index
                }
            }
        }

        VsText {
            visible: page.capture.mode === 3
            text: "Length of the largest object (mm)"
            variant: VsText.Caption
        }
        Row {
            visible: page.capture.mode === 3
            spacing: Theme.spacing

            VsNumberInput {
                id: minInput

                objectName: "minInput"
                width: (form.width - Theme.spacing) / 2
                from: 0
                to: 1000
                decimals: 1
                unit: "min"
                enabled: !page.locked
                onEdited: v => page.capture.minMm = v

                Binding {
                    target: minInput
                    property: "value"
                    value: page.capture.minMm
                }
            }
            VsNumberInput {
                id: maxInput

                objectName: "maxInput"
                width: (form.width - Theme.spacing) / 2
                from: 0
                to: 1000
                decimals: 1
                unit: "max"
                enabled: !page.locked
                onEdited: v => page.capture.maxMm = v

                Binding {
                    target: maxInput
                    property: "value"
                    value: page.capture.maxMm
                }
            }
        }

        VsText {
            text: "Folder (on the PC that runs the service)"
            variant: VsText.Caption
        }
        Row {
            spacing: Theme.spacing

            TextField {
                id: folderField

                objectName: "folderField"
                width: form.width - browse.width - Theme.spacing
                implicitHeight: Theme.controlHeight
                enabled: !page.locked
                text: page.capture.folder
                selectByMouse: true
                font.family: Theme.fontFamily
                font.pixelSize: Theme.sizeBody
                color: enabled ? Theme.textPrimary : Theme.textDisabled
                selectionColor: Theme.accent
                leftPadding: Theme.padding
                onEditingFinished: page.capture.folder = text

                background: Rectangle {
                    radius: Theme.radius
                    color: Theme.surface
                    border.width: Theme.borderWidth
                    border.color: folderField.activeFocus ? Theme.accent : Theme.border
                }
            }
            VsButton {
                id: browse

                objectName: "browseButton"
                text: "Browse..."
                visible: picker.status === Loader.Ready
                enabled: !page.locked
                onClicked: picker.item.open()
            }
        }

        Row {
            spacing: Theme.spacing

            VsButton {
                objectName: "startButton"
                text: "Start"
                primary: true
                enabled: page.capture.canStart
                onClicked: {
                    folderField.focus = false; // commits a typed folder first
                    page.capture.start();
                }
            }
            VsButton {
                objectName: "stopButton"
                text: "Stop"
                enabled: page.capture.running && !page.capture.busy
                onClicked: page.capture.stop()
            }
        }

        VsText {
            objectName: "statusText"
            width: parent.width
            text: page.capture.statusText
            color: page.capture.statusText.startsWith("Failed") ? Theme.error : Theme.textPrimary
        }
        VsText {
            width: parent.width
            visible: page.capture.sessionFolder !== ""
            text: "Photos are in " + page.capture.sessionFolder
            variant: VsText.Caption
        }
    }

    Loader {
        id: picker

        source: "CaptureFolderPicker.qml"
    }
    Connections {
        target: picker.item
        ignoreUnknownSignals: true

        function onPicked(path) {
            page.capture.folder = path;
        }
    }

    Rectangle {
        anchors {
            left: form.right
            leftMargin: Theme.padding
            right: parent.right
            rightMargin: Theme.padding
            top: parent.top
            topMargin: Theme.padding
            bottom: parent.bottom
            bottomMargin: Theme.padding
        }
        radius: Theme.radius
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: Theme.border

        Item {
            id: listHeader

            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
            }
            height: Theme.controlHeight + 2 * Theme.spacing

            VsText {
                anchors {
                    left: parent.left
                    leftMargin: Theme.padding
                    verticalCenter: parent.verticalCenter
                }
                text: "Sensors"
                variant: VsText.Title
            }
            Row {
                anchors {
                    right: parent.right
                    rightMargin: Theme.padding
                    verticalCenter: parent.verticalCenter
                }
                spacing: Theme.spacing

                VsButton {
                    objectName: "allButton"
                    text: "All"
                    implicitWidth: 80
                    enabled: !page.locked
                    onClicked: page.capture.selectAll(true)
                }
                VsButton {
                    objectName: "noneButton"
                    text: "None"
                    implicitWidth: 80
                    enabled: !page.locked
                    onClicked: page.capture.selectAll(false)
                }
            }
        }

        ListView {
            id: sensorList

            objectName: "sensorList"
            anchors {
                left: parent.left
                right: parent.right
                top: listHeader.bottom
                bottom: parent.bottom
                margins: Theme.padding
            }
            clip: true
            model: page.capture

            delegate: Column {
                id: entry

                required property int index
                required property int sensorId
                required property int cameraId
                required property bool selected
                required property bool cameraSelected
                required property bool firstOfCamera
                required property int saved
                required property int target

                width: ListView.view.width

                Item {
                    visible: entry.firstOfCamera
                    width: parent.width
                    height: visible ? Theme.controlHeight + Theme.spacing : 0

                    VsText {
                        anchors {
                            left: parent.left
                            verticalCenter: parent.verticalCenter
                        }
                        text: "Camera " + entry.cameraId
                        variant: VsText.Caption
                        font.letterSpacing: 1.5
                    }
                    VsButton {
                        objectName: "camera" + entry.cameraId + "Toggle"
                        anchors {
                            right: parent.right
                            verticalCenter: parent.verticalCenter
                        }
                        implicitWidth: 120
                        text: entry.cameraSelected ? "Camera on" : "Camera off"
                        primary: entry.cameraSelected
                        enabled: !page.locked
                        onClicked: page.capture.setCameraSelected(entry.cameraId, !entry.cameraSelected)
                    }
                }
                Item {
                    width: parent.width
                    height: Theme.controlHeight + Theme.spacing

                    VsText {
                        anchors {
                            left: parent.left
                            leftMargin: Theme.padding
                            verticalCenter: parent.verticalCenter
                        }
                        text: "Sensor " + entry.sensorId
                        color: entry.selected ? Theme.textPrimary : Theme.textDisabled
                    }
                    VsText {
                        anchors {
                            right: sensorToggle.left
                            rightMargin: Theme.padding
                            verticalCenter: parent.verticalCenter
                        }
                        visible: entry.selected && entry.target > 0
                        text: entry.saved + " / " + entry.target
                        color: entry.saved >= entry.target ? Theme.ok : Theme.textSecondary
                    }
                    VsButton {
                        id: sensorToggle

                        objectName: "sensor" + entry.sensorId + "Toggle"
                        anchors {
                            right: parent.right
                            verticalCenter: parent.verticalCenter
                        }
                        implicitWidth: 80
                        text: entry.selected ? "On" : "Off"
                        primary: entry.selected
                        enabled: !page.locked
                        onClicked: page.capture.setSelected(entry.index, !entry.selected)
                    }
                }
            }
        }

        VsText {
            anchors.centerIn: sensorList
            visible: sensorList.count === 0
            width: sensorList.width - 2 * Theme.padding
            horizontalAlignment: Text.AlignHCenter
            text: "No sensors. The service is offline, or photo capture is not available (analysis disabled)."
            variant: VsText.Caption
        }
    }
}
