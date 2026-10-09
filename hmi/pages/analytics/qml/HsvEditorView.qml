import QtQuick
import QtQuick.Controls.Basic
import VsortHmi

// G60.20 Parameters, HSV editor (P60.45): live pictures of the selected camera in a 2 x 2 grid.
// H, S and V: the channel as grey (0..255, H scaled from 0..179) with the pixels inside that
// channel's range 50 % green; beside each picture the histogram of the channel with the range
// shaded and a range slider (min / max). Fourth: the result, the colour picture with the pixels
// inside all three ranges green (what the segmentation keeps before erode/dilate). Save writes the
// range for every sensor of this camera into the "analysis" config; the service applies it
// without a restart.
Rectangle {
    id: page

    required property var live // LiveViewModel
    required property var tuning // AnalysisTuningModel
    property int cameraIndex: 0 // row of the selected camera
    property int cameraId: -1

    readonly property var channels: [
        {
            name: "H",
            title: "Hue",
            max: 179
        },
        {
            name: "S",
            title: "Saturation",
            max: 255
        },
        {
            name: "V",
            title: "Value",
            max: 255
        }
    ]

    // Shortcuts for attach().
    readonly property var hView: hTile.view
    readonly property var sView: sTile.view
    readonly property var vView: vTile.view
    readonly property var resultView: resultTile.view

    function attach() {
        for (const item of [hView, sView, vView, resultView]) {
            item.clear();
            page.live.attachVideo(page.cameraId, item); // attaching again moves the picture
        }
        page.tuning.selectCamera(page.cameraId);
    }

    color: Theme.background
    onCameraIdChanged: page.attach()

    Connections {
        target: page.live
        function onModelReset() {
            page.cameraIndex = 0;
        }
    }

    // Live picture (an HsvViewItem) with the "Waiting for frames" hint.
    component Picture: Item {
        id: picture

        property alias view: item
        property alias mode: item.view
        property string itemName: ""

        HsvViewItem {
            id: item

            objectName: picture.itemName
            anchors.fill: parent
            hsvLower: page.tuning.hsvLower
            hsvUpper: page.tuning.hsvUpper
        }

        VsText {
            anchors.centerIn: parent
            visible: !item.hasFrame
            color: Theme.textSecondary
            text: page.live.connected ? "Waiting for frames…" : "Service offline"
        }
    }

    // One channel: grey picture with the green mask, beside it the histogram and the range slider.
    component ChannelTile: Rectangle {
        id: tile

        required property int channel
        property string itemName: ""
        readonly property alias view: picture.view

        readonly property var spec: page.channels[tile.channel]
        readonly property int lo: page.tuning.hsvLower[tile.channel]
        readonly property int hi: page.tuning.hsvUpper[tile.channel]

        color: Theme.surface

        Picture {
            id: picture

            anchors {
                left: parent.left
                top: parent.top
                bottom: parent.bottom
                right: controls.left
                margins: 2
            }
            mode: tile.channel
            itemName: tile.itemName
        }

        Column {
            id: controls

            anchors {
                right: parent.right
                top: parent.top
                rightMargin: Theme.padding
                leftMargin: Theme.padding
                topMargin: Theme.padding
            }
            width: 240
            spacing: Theme.spacing

            VsText {
                text: tile.spec.name + " · " + tile.spec.title
                font.weight: Font.DemiBold
            }
            VsText {
                objectName: "rangeText"
                variant: VsText.Caption
                text: tile.lo + " – " + tile.hi + "   ·   " + picture.view.inRangePercent.toFixed(1) + " % in range"
            }

            Canvas {
                id: plot

                objectName: "histogram"
                width: parent.width
                height: 64

                onPaint: {
                    const ctx = getContext("2d");
                    ctx.reset();
                    const n = tile.spec.max + 1;
                    const bw = width / n;
                    const strip = tile.channel === 0 ? 6 : 0; // hue colours under the H plot
                    const h = height - strip;
                    ctx.fillStyle = Theme.surfaceRaised;
                    ctx.fillRect(0, 0, width, h);
                    ctx.fillStyle = Qt.rgba(0.0, 1.0, 0.0, 0.25);
                    ctx.fillRect(tile.lo * bw, 0, (tile.hi - tile.lo + 1) * bw, h);
                    const counts = picture.view.histogram;
                    let peak = 1;
                    for (let i = 0; i < counts.length; ++i)
                        peak = Math.max(peak, counts[i]);
                    const scale = (h - 2) / Math.sqrt(peak); // square root: small populations stay visible
                    ctx.fillStyle = Theme.textSecondary;
                    for (let i = 0; i < counts.length; ++i) {
                        const bar = Math.sqrt(counts[i]) * scale;
                        ctx.fillRect(i * bw, h - bar, Math.max(1, bw), bar);
                    }
                    for (let i = 0; i < n && strip > 0; ++i) {
                        ctx.fillStyle = Qt.hsla(i / n, 1, 0.5, 1);
                        ctx.fillRect(i * bw, h, Math.max(1, bw), strip);
                    }
                }

                Connections {
                    target: picture.view
                    function onStatsChanged() {
                        plot.requestPaint();
                    }
                }
                Connections {
                    target: page.tuning
                    function onRangeChanged() {
                        plot.requestPaint();
                    }
                }
            }

            RangeSlider {
                objectName: "rangeSlider"
                width: parent.width
                from: 0
                to: tile.spec.max
                stepSize: 1
                snapMode: RangeSlider.SnapAlways
                first.value: tile.lo
                second.value: tile.hi
                first.onMoved: page.tuning.setValue(0, tile.channel, Math.round(first.value))
                second.onMoved: page.tuning.setValue(1, tile.channel, Math.round(second.value))
            }

            Item {
                width: parent.width
                height: minText.implicitHeight

                VsText {
                    id: minText

                    anchors.left: parent.left
                    variant: VsText.Caption
                    text: "min " + tile.lo
                }
                VsText {
                    anchors.right: parent.right
                    variant: VsText.Caption
                    text: "max " + tile.hi
                }
            }
        }
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

            Repeater {
                model: page.live
                delegate: VsButton {
                    id: camButton

                    required property int index
                    required property int cameraId

                    objectName: "cameraButton"
                    text: "Camera " + camButton.cameraId
                    primary: camButton.index === page.cameraIndex
                    onClicked: page.cameraIndex = camButton.index

                    Binding {
                        target: page
                        property: "cameraId"
                        value: camButton.cameraId
                        when: camButton.index === page.cameraIndex
                    }
                }
            }

            VsText {
                anchors.verticalCenter: parent.verticalCenter
                variant: VsText.Caption
                text: page.tuning.sensors !== "" ? "Applies to: " + page.tuning.sensors : "No sensor uses this camera"
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

    Grid {
        id: grid

        readonly property real cellW: (width - spacing) / 2
        readonly property real cellH: (height - spacing) / 2

        anchors {
            left: parent.left
            right: parent.right
            top: bar.bottom
            bottom: parent.bottom
            margins: Theme.spacing
        }
        columns: 2
        spacing: Theme.spacing

        ChannelTile {
            id: hTile

            width: grid.cellW
            height: grid.cellH
            channel: 0
            itemName: "hView"
        }
        ChannelTile {
            id: sTile

            width: grid.cellW
            height: grid.cellH
            channel: 1
            itemName: "sView"
        }
        ChannelTile {
            id: vTile

            width: grid.cellW
            height: grid.cellH
            channel: 2
            itemName: "vView"
        }

        // Result: colour picture, green = inside all three ranges.
        Rectangle {
            id: resultTile

            readonly property alias view: resultPicture.view

            width: grid.cellW
            height: grid.cellH
            color: Theme.surface

            Picture {
                id: resultPicture

                anchors {
                    left: parent.left
                    top: parent.top
                    bottom: parent.bottom
                    right: resultInfo.left
                    margins: 2
                }
                mode: 3
                itemName: "resultView"
            }

            Column {
                id: resultInfo

                anchors {
                    right: parent.right
                    top: parent.top
                    rightMargin: Theme.padding
                    topMargin: Theme.padding
                }
                width: 240
                spacing: Theme.spacing

                VsText {
                    text: "Result"
                    font.weight: Font.DemiBold
                }
                VsText {
                    objectName: "resultText"
                    variant: VsText.Caption
                    text: resultPicture.view.inRangePercent.toFixed(1) + " % in all three ranges"
                }
                VsText {
                    width: parent.width
                    wrapMode: Text.Wrap
                    variant: VsText.Caption
                    text: "Green: kept by the segmentation, before erode/dilate and the object filter. Histograms: pixel count per value of the whole picture (square-root scale), shaded = range."
                }
            }
        }
    }
}
