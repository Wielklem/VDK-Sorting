import QtQuick
import VsortHmi

// G30.20 ROI editor (P30.80): draw, move and resize ROIs on the live picture, or type them in.
// ROIs are saved per camera as fractions of the camera image (config module "rois").
Rectangle {
    id: page

    required property var live // LiveViewModel
    required property var roi // RoiEditorModel

    readonly property int cameraId: picker.cameraId
    readonly property real frameW: video.sourceSize.width
    readonly property real frameH: video.sourceSize.height
    // The ROIs in frame pixels, as the overlay layer wants them.
    readonly property var overlayRois: page.roi.rois.map(r => ({
                x: r.x * page.frameW,
                y: r.y * page.frameH,
                width: r.width * page.frameW,
                height: r.height * page.frameH,
                label: r.label
            }))

    // Numeric field in percent that follows the model again after the user typed a value.
    component PctField: VsNumberInput {
        id: field

        property real source: 0 // percent, from the model

        from: 0
        to: 100
        decimals: 1
        unit: "%"

        Binding {
            target: field
            property: "value"
            value: field.source
        }
    }

    color: Theme.background
    focus: true
    Keys.onDeletePressed: page.roi.removeRoi(page.roi.selected)

    onCameraIdChanged: {
        video.clear();
        page.live.attachVideo(page.cameraId, video); // attaching again moves the video
        page.roi.selectCamera(page.cameraId);
    }

    Rectangle {
        id: bar

        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        height: Theme.controlHeight + 2 * Theme.spacing
        color: Theme.background

        VsText {
            id: title

            anchors {
                left: parent.left
                leftMargin: Theme.padding
                verticalCenter: parent.verticalCenter
            }
            text: "ROI editor"
            variant: VsText.Title
        }

        VsCameraPicker {
            id: picker

            live: page.live
            showFreeze: true
            anchors {
                left: title.right
                leftMargin: Theme.padding
                verticalCenter: parent.verticalCenter
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
                objectName: "reloadButton"
                text: "Reload"
                onClicked: page.roi.reload()
            }
            VsButton {
                objectName: "saveButton"
                text: "Save"
                primary: page.roi.dirty
                enabled: page.roi.dirty
                onClicked: page.roi.save()
            }
        }
    }

    Rectangle {
        id: panel

        anchors {
            right: parent.right
            top: bar.bottom
            bottom: parent.bottom
        }
        width: 340
        color: Theme.surface

        Column {
            anchors {
                fill: parent
                margins: Theme.padding
            }
            spacing: Theme.spacing

            VsText {
                width: parent.width
                text: "Regions of interest"
                variant: VsText.Title
            }

            ListView {
                id: list

                objectName: "roiList"
                width: parent.width
                height: 160
                clip: true
                model: page.roi
                delegate: Rectangle {
                    id: listRow

                    required property int index
                    required property string roiName

                    objectName: "roiRow"
                    width: list.width
                    height: Theme.controlHeight
                    color: page.roi.selected === listRow.index ? Theme.accent : "transparent"

                    VsText {
                        anchors {
                            left: parent.left
                            leftMargin: Theme.spacing
                            verticalCenter: parent.verticalCenter
                        }
                        text: listRow.roiName
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: page.roi.select(listRow.index)
                    }
                }
            }

            Row {
                spacing: Theme.spacing

                VsButton {
                    objectName: "addButton"
                    text: "Add"
                    enabled: page.cameraId >= 0
                    onClicked: page.roi.addRoi(0.25, 0.25, 0.75, 0.75)
                }
                VsButton {
                    objectName: "deleteButton"
                    text: "Delete"
                    enabled: page.roi.selected >= 0
                    onClicked: page.roi.removeRoi(page.roi.selected)
                }
            }

            Grid {
                columns: 2
                columnSpacing: Theme.spacing
                rowSpacing: Theme.spacing
                verticalItemAlignment: Grid.AlignVCenter

                VsText {
                    text: "X"
                }
                PctField {
                    id: xField

                    objectName: "xInput"
                    width: 180
                    enabled: page.roi.selected >= 0
                    source: page.roi.selectedRect.x * 100
                    onEdited: v => {
                        page.roi.moveTo(page.roi.selected, v / 100, page.roi.selectedRect.y);
                        xField.value = xField.source;
                    }
                }

                VsText {
                    text: "Y"
                }
                PctField {
                    id: yField

                    objectName: "yInput"
                    width: 180
                    enabled: page.roi.selected >= 0
                    source: page.roi.selectedRect.y * 100
                    onEdited: v => {
                        page.roi.moveTo(page.roi.selected, page.roi.selectedRect.x, v / 100);
                        yField.value = yField.source;
                    }
                }

                VsText {
                    text: "Width"
                }
                PctField {
                    id: wField

                    objectName: "widthInput"
                    width: 180
                    enabled: page.roi.selected >= 0
                    source: page.roi.selectedRect.width * 100
                    onEdited: v => {
                        const r = page.roi.selectedRect;
                        page.roi.setRect(page.roi.selected, r.x, r.y, v / 100, r.height);
                        wField.value = wField.source;
                    }
                }

                VsText {
                    text: "Height"
                }
                PctField {
                    id: hField

                    objectName: "heightInput"
                    width: 180
                    enabled: page.roi.selected >= 0
                    source: page.roi.selectedRect.height * 100
                    onEdited: v => {
                        const r = page.roi.selectedRect;
                        page.roi.setRect(page.roi.selected, r.x, r.y, r.width, v / 100);
                        hField.value = hField.source;
                    }
                }
            }

            VsText {
                width: parent.width
                variant: VsText.Caption
                text: "Drag on the picture to draw a ROI. Drag a ROI to move it, drag a corner to resize it. Values are percent of the image."
            }
            VsText {
                objectName: "statusText"
                width: parent.width
                variant: VsText.Caption
                color: Theme.warn
                text: page.roi.status
            }
        }
    }

    Item {
        id: stage

        anchors {
            left: parent.left
            right: panel.left
            top: bar.bottom
            bottom: parent.bottom
        }

        VideoItem {
            id: video

            objectName: "video"
            anchors.fill: parent
        }

        OverlayLayer {
            objectName: "overlay"
            anchors.fill: parent
            sourceSize: video.sourceSize
            rois: page.overlayRois
        }

        // Exactly the rect where the picture is drawn; ROI fractions map onto it directly.
        Item {
            id: pic

            readonly property real k: video.sourceSize.width > 0 && video.sourceSize.height > 0 ? Math.min(stage.width / video.sourceSize.width, stage.height / video.sourceSize.height) : 0

            objectName: "picture"
            visible: k > 0
            width: video.sourceSize.width * k
            height: video.sourceSize.height * k
            x: (stage.width - width) / 2
            y: (stage.height - height) / 2

            // Empty picture area: draw a new ROI.
            MouseArea {
                id: drawArea

                property real x0: 0
                property real y0: 0
                property real x1: 0
                property real y1: 0
                property bool drawing: false

                function norm(v, size) {
                    return Math.min(1, Math.max(0, v / size));
                }

                anchors.fill: parent
                onPressed: mouse => {
                    page.roi.select(-1);
                    drawArea.x0 = norm(mouse.x, width);
                    drawArea.y0 = norm(mouse.y, height);
                    drawArea.x1 = drawArea.x0;
                    drawArea.y1 = drawArea.y0;
                    drawArea.drawing = true;
                }
                onPositionChanged: mouse => {
                    if (drawArea.drawing) {
                        drawArea.x1 = norm(mouse.x, width);
                        drawArea.y1 = norm(mouse.y, height);
                    }
                }
                onReleased: {
                    if (drawArea.drawing) {
                        drawArea.drawing = false;
                        page.roi.addRoi(drawArea.x0, drawArea.y0, drawArea.x1, drawArea.y1);
                    }
                }
                onCanceled: drawArea.drawing = false
            }

            Rectangle {
                visible: drawArea.drawing
                x: Math.min(drawArea.x0, drawArea.x1) * pic.width
                y: Math.min(drawArea.y0, drawArea.y1) * pic.height
                width: Math.abs(drawArea.x1 - drawArea.x0) * pic.width
                height: Math.abs(drawArea.y1 - drawArea.y0) * pic.height
                color: "transparent"
                border.width: 2
                border.color: Theme.warn
            }

            Repeater {
                model: page.roi
                delegate: Item {
                    id: box

                    required property int index
                    required property real roiX
                    required property real roiY
                    required property real roiW
                    required property real roiH
                    readonly property bool isSelected: page.roi.selected === box.index

                    objectName: "roiBox"
                    x: box.roiX * pic.width
                    y: box.roiY * pic.height
                    width: box.roiW * pic.width
                    height: box.roiH * pic.height

                    Rectangle {
                        anchors.fill: parent
                        visible: box.isSelected
                        color: "#332f81f7"
                        border.width: 3
                        border.color: Theme.warn
                    }

                    // Body: select and move.
                    MouseArea {
                        id: moveArea

                        property real grabX: 0
                        property real grabY: 0

                        anchors.fill: parent
                        onPressed: mouse => {
                            page.roi.select(box.index);
                            const p = moveArea.mapToItem(pic, mouse.x, mouse.y);
                            moveArea.grabX = p.x - box.x;
                            moveArea.grabY = p.y - box.y;
                        }
                        onPositionChanged: mouse => {
                            if (!moveArea.pressed) {
                                return;
                            }
                            const p = moveArea.mapToItem(pic, mouse.x, mouse.y);
                            page.roi.moveTo(box.index, (p.x - moveArea.grabX) / pic.width, (p.y - moveArea.grabY) / pic.height);
                        }
                    }

                    // Corner handles of the selected ROI: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right.
                    Repeater {
                        model: box.isSelected ? 4 : 0
                        delegate: Rectangle {
                            id: knob

                            required property int index
                            readonly property int cx: knob.index % 2
                            readonly property int cy: Math.floor(knob.index / 2)

                            objectName: "roiHandle"
                            width: 20
                            height: 20
                            radius: 3
                            color: Theme.warn
                            x: knob.cx * box.width - width / 2
                            y: knob.cy * box.height - height / 2

                            MouseArea {
                                id: knobArea

                                property real fixedX: 0
                                property real fixedY: 0

                                anchors.fill: parent
                                anchors.margins: -8
                                onPressed: {
                                    // The corner opposite to the grabbed one stays where it is.
                                    knobArea.fixedX = knob.cx === 0 ? box.roiX + box.roiW : box.roiX;
                                    knobArea.fixedY = knob.cy === 0 ? box.roiY + box.roiH : box.roiY;
                                }
                                onPositionChanged: mouse => {
                                    if (!knobArea.pressed) {
                                        return;
                                    }
                                    const p = knobArea.mapToItem(pic, mouse.x, mouse.y);
                                    const px = Math.min(1, Math.max(0, p.x / pic.width));
                                    const py = Math.min(1, Math.max(0, p.y / pic.height));
                                    page.roi.setRect(box.index, Math.min(knobArea.fixedX, px), Math.min(knobArea.fixedY, py), Math.abs(px - knobArea.fixedX), Math.abs(py - knobArea.fixedY));
                                }
                            }
                        }
                    }
                }
            }
        }

        VsText {
            anchors.centerIn: parent
            visible: !video.hasFrame
            color: Theme.textSecondary
            text: page.live.connected ? "Waiting for frames…" : "Service offline"
        }
    }
}
