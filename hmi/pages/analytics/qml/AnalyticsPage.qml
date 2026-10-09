import QtQuick
import VsortHmi

// G60 Analytics: side navigation with "Contour (HSV)" (G60.20 Parameters, HSV editor, P60.45) and
// "Detector" (G60.30, found objects and detector parameters, P60.90). The selected camera is kept
// when switching tabs. `liveView`, `analysisTuning`, `roiOverlay` and `detectorModel` are the
// models that main.cpp puts into the QML context.
Item {
    id: root

    property int tab: 0
    property int cameraIndex: 0
    readonly property var tabNames: ["Contour (HSV)", "Detector"]
    readonly property var tabObjectNames: ["tabContour", "tabDetector"]

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
                text: "ANALYTICS"
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
        sourceComponent: root.tab === 0 ? contourTab : detectorTab
    }

    Component {
        id: contourTab

        HsvEditorView {
            live: liveView
            tuning: analysisTuning
            rois: roiOverlay
            cameraIndex: root.cameraIndex
            onCameraIndexChanged: root.cameraIndex = cameraIndex
        }
    }
    Component {
        id: detectorTab

        DetectorView {
            live: liveView
            tuning: analysisTuning
            rois: roiOverlay
            detector: detectorModel
            cameraIndex: root.cameraIndex
            onCameraIndexChanged: root.cameraIndex = cameraIndex
        }
    }
}
