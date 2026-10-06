import QtQuick
import VsortHmi

// G30 Cameras: tabs G30.10 Live view, G30.20 ROI, G30.30 Camera settings.
// `liveView`, `roiEditor` and `cameraSettings` are the models that main.cpp puts into the QML context.
Item {
    id: root

    property int tab: 0

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
                objectName: "tabLive"
                text: "Live view"
                primary: root.tab === 0
                onClicked: root.tab = 0
            }
            VsButton {
                objectName: "tabRoi"
                text: "ROI"
                primary: root.tab === 1
                onClicked: root.tab = 1
            }
            VsButton {
                objectName: "tabSettings"
                text: "Camera settings"
                primary: root.tab === 2
                onClicked: root.tab = 2
            }
        }
    }

    Loader {
        anchors {
            left: parent.left
            right: parent.right
            top: tabs.bottom
            bottom: parent.bottom
        }
        sourceComponent: root.tab === 0 ? liveTab : (root.tab === 1 ? roiTab : settingsTab)
    }

    Component {
        id: liveTab

        LiveViewPage {
            live: liveView
        }
    }
    Component {
        id: roiTab

        RoiEditorPage {
            live: liveView
            roi: roiEditor
        }
    }
    Component {
        id: settingsTab

        CameraSettingsPage {
            live: liveView
            settings: cameraSettings
        }
    }
}