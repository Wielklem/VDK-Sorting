import QtQuick
import VsortHmi

// G30 Cameras: tabs G30.10 Live view, G30.20 ROI, G30.30 Camera settings, as side navigation on the left.
// `liveView`, `roiEditor` and `cameraSettings` are the models that main.cpp puts into the QML context.
Item {
    id: root

    property int tab: 0
    readonly property var tabNames: ["Live view", "ROI", "Camera settings"]
    readonly property var tabObjectNames: ["tabLive", "tabRoi", "tabSettings"]

    Rectangle {
        id: tabs

        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
        }
        width: Theme.sideNavWidth
        color: Theme.surface

        Column {
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                margins: Theme.spacing
                topMargin: Theme.padding
            }
            spacing: Theme.spacing / 2

            VsText {
                leftPadding: Theme.padding
                bottomPadding: Theme.spacing
                text: "CAMERAS"
                variant: VsText.Caption
                font.letterSpacing: 1.5
            }

            Repeater {
                model: root.tabNames
                delegate: VsSideTab {
                    required property int index
                    required property string modelData

                    width: parent.width
                    objectName: root.tabObjectNames[index]
                    text: modelData
                    selected: root.tab === index
                    onClicked: root.tab = index
                }
            }
        }

        Rectangle {
            anchors {
                right: parent.right
                top: parent.top
                bottom: parent.bottom
            }
            width: Theme.borderWidth
            color: Theme.border
        }
    }

    Loader {
        anchors {
            left: tabs.right
            right: parent.right
            top: parent.top
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
