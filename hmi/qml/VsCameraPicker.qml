import QtQuick
import QtQuick.Controls.Basic

// Camera dropdown, optionally with a Freeze/Unfreeze button for the selected camera (freeze is
// the HMI-only display freeze of the LiveViewModel, same as on G30.10). `cameraId` follows the
// selection (-1 while there are no cameras); `frozen` is the freeze state of that camera.
Row {
    id: picker

    required property var live // LiveViewModel
    property bool showFreeze: false
    property int cameraId: -1 // set by the Binding of the selected row below
    property bool frozen: false // same
    property alias currentIndex: combo.currentIndex // row of the selected camera

    spacing: Theme.spacing

    Connections {
        target: picker.live
        function onModelReset() {
            combo.currentIndex = combo.count > 0 ? 0 : -1;
        }
    }

    ComboBox {
        id: combo

        objectName: "cameraCombo"
        anchors.verticalCenter: parent.verticalCenter
        implicitWidth: 200
        implicitHeight: Theme.controlHeight
        model: picker.live
        textRole: "cameraId"
        displayText: picker.cameraId >= 0 ? "Camera " + picker.cameraId : "No camera"
        onCountChanged: {
            if (combo.currentIndex < 0 && combo.count > 0)
                combo.currentIndex = 0;
        }

        delegate: ItemDelegate {
            required property int index
            required property int cameraId

            width: combo.width
            text: "Camera " + cameraId
            highlighted: combo.highlightedIndex === index
        }

        contentItem: VsText {
            leftPadding: Theme.spacing
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
            text: combo.displayText
        }

        background: Rectangle {
            radius: Theme.radius
            color: combo.pressed ? Theme.surface : Theme.surfaceRaised
            border.width: Theme.borderWidth
            border.color: combo.activeFocus ? Theme.accent : Theme.border
        }
    }

    // Mirror of the model rows: hands cameraId and frozen of the selected row to the picker.
    Repeater {
        model: picker.live
        delegate: Item {
            id: row

            required property int index
            required property int cameraId
            required property bool frozen

            Binding {
                target: picker
                property: "cameraId"
                value: row.cameraId
                when: row.index === combo.currentIndex
            }
            Binding {
                target: picker
                property: "frozen"
                value: row.frozen
                when: row.index === combo.currentIndex
            }
        }
    }

    VsButton {
        objectName: "cameraFreezeButton"
        anchors.verticalCenter: parent.verticalCenter
        visible: picker.showFreeze
        enabled: picker.cameraId >= 0
        primary: picker.frozen
        text: picker.frozen ? "Unfreeze" : "Freeze"
        onClicked: picker.live.toggleFrozen(picker.cameraId)
    }
}
