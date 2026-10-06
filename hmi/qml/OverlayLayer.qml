import QtQuick

// P30.70 Overlay layer: ROIs, lane lines and detections on top of a VideoItem.
// Give it the same geometry as the VideoItem and bind sourceSize to the VideoItem's sourceSize.
// All coordinates are camera-frame pixels. They are mapped with the same aspect-fit rule as
// VideoItem, so the overlay stays on the picture at any size. It never takes mouse input.
Item {
    id: ol

    property size sourceSize: Qt.size(0, 0)
    property var rois: [] // [{x, y, width, height, label}]
    property var lanes: [] // [{x1, y1, x2, y2}]
    property var detections: [] // [{x, y, width, height, label}]
    property bool showRois: true
    property bool showLanes: true
    property bool showDetections: true

    readonly property bool valid: sourceSize.width > 0 && sourceSize.height > 0 && width > 0 && height > 0
    readonly property real pxScale: valid ? Math.min(width / sourceSize.width, height / sourceSize.height) : 0

    // Numeric field of list[index]; 0 while the entry is missing (delegate outlives its data briefly).
    function field(list, index, name) {
        const entry = list[index];
        return entry !== undefined && typeof entry[name] === "number" ? entry[name] : 0;
    }

    function label(list, index) {
        const entry = list[index];
        return entry !== undefined && entry.label !== undefined ? String(entry.label) : "";
    }

    // Exactly the rect where the picture is drawn.
    Item {
        id: content

        objectName: "overlayContent"
        visible: ol.valid
        clip: true
        width: ol.sourceSize.width * ol.pxScale
        height: ol.sourceSize.height * ol.pxScale
        x: (ol.width - width) / 2
        y: (ol.height - height) / 2

        Repeater {
            model: ol.showRois ? ol.rois.length : 0
            delegate: Rectangle {
                required property int index

                objectName: "roi"
                x: ol.field(ol.rois, index, "x") * ol.pxScale
                y: ol.field(ol.rois, index, "y") * ol.pxScale
                width: ol.field(ol.rois, index, "width") * ol.pxScale
                height: ol.field(ol.rois, index, "height") * ol.pxScale
                color: "transparent"
                border.width: 2
                border.color: Theme.accent

                VsText {
                    anchors {
                        left: parent.left
                        top: parent.top
                        margins: 4
                    }
                    variant: VsText.Caption
                    color: Theme.accent
                    text: ol.label(ol.rois, parent.index)
                }
            }
        }

        Repeater {
            model: ol.showLanes ? ol.lanes.length : 0
            delegate: Rectangle {
                id: laneLine

                required property int index
                readonly property real dx: (ol.field(ol.lanes, index, "x2") - ol.field(ol.lanes, index, "x1")) * ol.pxScale
                readonly property real dy: (ol.field(ol.lanes, index, "y2") - ol.field(ol.lanes, index, "y1")) * ol.pxScale

                objectName: "laneLine"
                x: ol.field(ol.lanes, index, "x1") * ol.pxScale
                y: ol.field(ol.lanes, index, "y1") * ol.pxScale - height / 2
                width: Math.hypot(dx, dy)
                height: 2
                rotation: Math.atan2(dy, dx) * 180 / Math.PI
                transformOrigin: Item.Left
                color: Theme.warn
            }
        }

        Repeater {
            model: ol.showDetections ? ol.detections.length : 0
            delegate: Rectangle {
                required property int index

                objectName: "detection"
                x: ol.field(ol.detections, index, "x") * ol.pxScale
                y: ol.field(ol.detections, index, "y") * ol.pxScale
                width: ol.field(ol.detections, index, "width") * ol.pxScale
                height: ol.field(ol.detections, index, "height") * ol.pxScale
                color: "transparent"
                border.width: 2
                border.color: Theme.ok

                VsText {
                    anchors {
                        left: parent.left
                        bottom: parent.bottom
                        margins: 4
                    }
                    variant: VsText.Caption
                    color: Theme.ok
                    text: ol.label(ol.detections, parent.index)
                }
            }
        }
    }
}
