import QtQuick
import QtQuick.Controls.Basic
import VsortHmi

// G60.30 Detector (P60.90): what the service's pipeline found in the live picture of the selected
// camera: the outline of every object (white: counted for the cup, dimmed: centred in the buffer,
// so a neighbour cup) with its size, plus the camera's ROIs. Beside it the detector parameters of
// the camera's sensors. Save writes them (together with the HSV range) into the "analysis" config;
// the service applies them from the next frame, so the outlines show the effect right away.
Rectangle {
    id: page

    required property var live // LiveViewModel
    required property var tuning // AnalysisTuningModel
    required property var rois // RoiEditorModel, read only: the saved ROIs of the camera
    required property var detector // DetectorModel
    property int cameraIndex: 0 // shared with the other Analytics tab

    readonly property int cameraId: picker.cameraId
    readonly property real frameW: video.sourceSize.width
    readonly property real frameH: video.sourceSize.height
    // Preview pixels per camera-frame pixel (the preview is scaled down).
    readonly property real sx: page.detector.frameSize.width > 0 ? page.frameW / page.detector.frameSize.width : 0
    readonly property real sy: page.detector.frameSize.height > 0 ? page.frameH / page.detector.frameSize.height : 0

    function attach() {
        video.clear();
        page.live.attachVideo(page.cameraId, video); // attaching again moves the picture
        page.tuning.selectCamera(page.cameraId);
        page.rois.selectCamera(page.cameraId);
        page.detector.selectCamera(page.cameraId);
    }

    color: Theme.background
    onCameraIdChanged: page.attach()

    // One numeric parameter of `tuning.params`.
    component ParamField: Item {
        id: field

        required property string key
        required property string label
        property real from: 0
        property real to: 100000
        property int decimals: 0
        property string unit: ""

        width: parent ? parent.width : 0
        height: Theme.controlHeight

        VsText {
            anchors {
                left: parent.left
                right: input.left
                rightMargin: Theme.spacing
                verticalCenter: parent.verticalCenter
            }
            variant: VsText.Caption
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
            text: field.label + (field.unit !== "" ? " [" + field.unit + "]" : "")
        }

        VsNumberInput {
            id: input

            objectName: "param_" + field.key
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            width: 130
            from: field.from
            to: field.to
            decimals: field.decimals
            onEdited: v => page.tuning.setParam(field.key, v)

            Binding {
                target: input
                property: "value"
                value: page.tuning.params[field.key] !== undefined ? page.tuning.params[field.key] : 0
            }
        }
    }

    component Heading: VsText {
        topPadding: Theme.spacing
        font.weight: Font.DemiBold
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

            VsCameraPicker {
                id: picker

                anchors.verticalCenter: parent.verticalCenter
                live: page.live
                showFreeze: true
                Component.onCompleted: currentIndex = page.cameraIndex
                onCurrentIndexChanged: page.cameraIndex = currentIndex
            }

            CheckBox {
                id: roiToggle

                objectName: "roiToggle"
                anchors.verticalCenter: parent.verticalCenter
                checked: true
                text: "ROI"
                contentItem: VsText {
                    leftPadding: roiToggle.indicator.width + roiToggle.spacing
                    verticalAlignment: Text.AlignVCenter
                    text: roiToggle.text
                }
            }

            VsText {
                objectName: "detectorSummary"
                anchors.verticalCenter: parent.verticalCenter
                variant: VsText.Caption
                text: !page.detector.receiving ? "No results from the analysis for this camera" : (page.detector.matched ? page.detector.countedObjects + " in the ROI, " + page.detector.neighbourObjects + " neighbour" : "Waiting for the analysis of this frame")
            }
        }

        Row {
            anchors {
                right: parent.right
                rightMargin: Theme.padding
                verticalCenter: parent.verticalCenter
            }
            spacing: Theme.spacing

            VsText {
                objectName: "statusText"
                anchors.verticalCenter: parent.verticalCenter
                variant: VsText.Caption
                text: page.tuning.status
            }
            VsButton {
                objectName: "reloadButton"
                text: "Reload"
                onClicked: page.tuning.reload()
            }
            VsButton {
                objectName: "saveButton"
                text: "Save"
                primary: true
                enabled: page.tuning.canSave && page.tuning.dirty
                onClicked: page.tuning.save()
            }
        }
    }

    Item {
        id: picture

        anchors {
            left: parent.left
            right: panel.left
            top: bar.bottom
            bottom: parent.bottom
            margins: Theme.spacing
        }

        VideoItem {
            id: video

            objectName: "video"
            anchors.fill: parent
        }

        Binding {
            target: page.detector
            property: "frameId"
            value: video.frameId
        }

        OverlayLayer {
            objectName: "overlay"
            anchors.fill: parent
            sourceSize: video.sourceSize
            showRois: roiToggle.checked
            showLanes: false
            showDetections: false
            showContours: true
            rois: page.rois.rois.map(r => ({
                        x: r.x * page.frameW,
                        y: r.y * page.frameH,
                        width: r.width * page.frameW,
                        height: r.height * page.frameH,
                        label: r.label
                    }))
            contours: page.detector.objects.map(o => ({
                        points: o.points.map((v, i) => v * (i % 2 === 0 ? page.sx : page.sy)),
                        counted: o.counted,
                        label: o.label
                    }))
        }

        VsText {
            anchors.centerIn: parent
            visible: !video.hasFrame
            color: Theme.textSecondary
            text: page.live.connected ? "Waiting for frames…" : "Service offline"
        }
    }

    Rectangle {
        id: panel

        anchors {
            right: parent.right
            top: bar.bottom
            bottom: parent.bottom
        }
        width: 360
        color: Theme.surface

        ScrollView {
            anchors {
                fill: parent
                margins: Theme.padding
            }
            contentWidth: availableWidth
            clip: true

            Column {
                width: parent.width
                spacing: 2

                VsText {
                    text: "Detector parameters"
                    variant: VsText.Title
                }
                VsText {
                    width: parent.width
                    wrapMode: Text.Wrap
                    variant: VsText.Caption
                    text: page.tuning.sensors !== "" ? "Applies to: " + page.tuning.sensors : "No sensor uses this camera"
                }

                Heading {
                    text: "Mask clean-up"
                }
                ParamField {
                    key: "morph_kernel_px"
                    label: "Kernel size"
                    to: 51
                    unit: "px"
                }
                ParamField {
                    key: "erode_iterations"
                    label: "Erode iterations"
                    to: 20
                }
                ParamField {
                    key: "dilate_iterations"
                    label: "Dilate iterations"
                    to: 20
                }

                Heading {
                    text: "Object filter"
                }
                ParamField {
                    key: "min_blob_area_px"
                    label: "Min. area (noise)"
                    to: 1000000
                    unit: "px²"
                }
                ParamField {
                    key: "min_diameter_px"
                    label: "Min. diameter"
                    to: 10000
                    decimals: 1
                    unit: "px"
                }
                ParamField {
                    key: "min_hull_area_px"
                    label: "Min. hull area"
                    to: 10000000
                    unit: "px²"
                }
                ParamField {
                    key: "min_solidity"
                    label: "Min. solidity"
                    to: 1
                    decimals: 2
                }
                ParamField {
                    key: "max_aspect_ratio"
                    label: "Max. aspect ratio"
                    from: 1
                    to: 100
                    decimals: 1
                }

                Heading {
                    text: "Detection buffer around the ROI"
                }
                ParamField {
                    key: "roi_buffer_px.left"
                    label: "Left"
                    to: 10000
                    unit: "px"
                }
                ParamField {
                    key: "roi_buffer_px.right"
                    label: "Right"
                    to: 10000
                    unit: "px"
                }
                ParamField {
                    key: "roi_buffer_px.top"
                    label: "Top"
                    to: 10000
                    unit: "px"
                }
                ParamField {
                    key: "roi_buffer_px.bottom"
                    label: "Bottom"
                    to: 10000
                    unit: "px"
                }

                Heading {
                    text: "Size"
                }
                ParamField {
                    key: "roi_width_mm"
                    label: "ROI width (0 = use mm/px)"
                    to: 100000
                    decimals: 1
                    unit: "mm"
                }
                ParamField {
                    key: "mm_per_px"
                    label: "Scale"
                    from: 0.0001
                    to: 1000
                    decimals: 4
                    unit: "mm/px"
                }

                VsText {
                    width: parent.width
                    topPadding: Theme.spacing
                    wrapMode: Text.Wrap
                    variant: VsText.Caption
                    text: "Pixel values are camera-frame pixels. White outline: counted for the cup (centre inside the ROI); dimmed: centre in the buffer, belongs to a neighbour cup. Save applies the values from the next frame."
                }
            }
        }
    }
}
