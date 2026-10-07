
import QtQuick
import QtQuick.Controls.Basic
import VsortHmi

// G140.10 Product monitor (P80.110): the last cups of one lane, newest on top. Per sensor a photo
// column ("photo OK" or red "no data"; empty while the cup has not reached the sensor) plus its
// measurement columns (empty until the analysis stages exist). Columns follow the machine config.
Rectangle {
    id: page

    required property var monitor // ProductMonitorModel

    readonly property int cupColumnWidth: 110
    readonly property int cellHeight: Theme.controlHeight
    readonly property real cellWidth: Math.max(110, (list.width - page.cupColumnWidth) / Math.max(1, page.monitor.columns.length))
    readonly property bool ready: page.monitor.status === ""

    component HeaderCell: Rectangle {
        id: headerCell

        property string text: ""

        height: page.cellHeight
        color: Theme.surfaceRaised
        border.width: Theme.borderWidth
        border.color: Theme.background

        VsText {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing
            anchors.rightMargin: Theme.spacing
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
            variant: VsText.Caption
            font.weight: Font.DemiBold
            text: headerCell.text
        }
    }

    color: Theme.background

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

            VsText {
                anchors.verticalCenter: parent.verticalCenter
                text: "Lane"
            }

            ComboBox {
                id: laneBox

                objectName: "laneBox"
                width: 240
                height: Theme.controlHeight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.sizeBody
                // Dark theme for the Basic style: button, popup list and highlight.
                palette.button: Theme.surfaceRaised
                palette.buttonText: Theme.textPrimary
                palette.window: Theme.surfaceRaised
                palette.base: Theme.surface
                palette.text: Theme.textPrimary
                palette.windowText: Theme.textPrimary
                palette.highlight: Theme.accent
                palette.highlightedText: Theme.textOnAccent
                palette.dark: Theme.textSecondary
                palette.mid: Theme.border
                palette.light: Theme.surfaceRaised
                model: page.monitor.lanes
                textRole: "name"
                currentIndex: page.monitor.laneIndex
                enabled: page.ready
                onActivated: index => page.monitor.laneIndex = index
            }
        }

        VsText {
            objectName: "liveText"
            anchors {
                right: parent.right
                rightMargin: Theme.padding
                verticalCenter: parent.verticalCenter
            }
            visible: page.ready
            text: page.monitor.frozen ? "Frozen" : "Live"
            color: page.monitor.frozen ? Theme.warn : Theme.ok
        }
    }

    Row {
        id: header

        anchors {
            left: parent.left
            right: parent.right
            top: bar.bottom
            topMargin: Theme.spacing
            leftMargin: Theme.padding
            rightMargin: Theme.padding
        }
        height: page.cellHeight
        visible: page.ready

        HeaderCell {
            width: page.cupColumnWidth
            text: "Cup"
        }

        Repeater {
            model: page.monitor.columns
            delegate: HeaderCell {
                required property var modelData

                objectName: "columnHeader"
                width: page.cellWidth
                text: modelData.title
            }
        }
    }

    ListView {
        id: list

        objectName: "cupList"
        anchors {
            left: header.left
            right: header.right
            top: header.bottom
            bottom: freezeButton.top
            bottomMargin: Theme.spacing
        }
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        visible: page.ready
        model: page.monitor.rows

        delegate: Row {
            id: rowItem

            required property int index
            required property var cupId
            required property var cells

            readonly property color rowColor: rowItem.index % 2 === 0 ? Theme.surface : Theme.surfaceRaised

            height: page.cellHeight

            Rectangle {
                width: page.cupColumnWidth
                height: page.cellHeight
                color: rowItem.rowColor
                border.width: Theme.borderWidth
                border.color: Theme.background

                VsText {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.NoWrap
                    text: String(rowItem.cupId)
                }
            }

            Repeater {
                model: rowItem.cells
                delegate: Rectangle {
                    id: cell

                    required property string modelData

                    objectName: cell.modelData === "nodata" ? "noDataCell" : (cell.modelData === "ok" ? "okCell" : "cell")
                    width: page.cellWidth
                    height: page.cellHeight
                    color: cell.modelData === "nodata" ? Theme.error : rowItem.rowColor
                    border.width: Theme.borderWidth
                    border.color: Theme.background

                    VsText {
                        anchors.centerIn: parent
                        wrapMode: Text.NoWrap
                        variant: VsText.Caption
                        color: cell.modelData === "nodata" ? Theme.textOnAccent : (cell.modelData === "ok" ? Theme.ok : Theme.textDisabled)
                        text: {
                            switch (cell.modelData) {
                            case "ok":
                                return "photo OK";
                            case "nodata":
                                return "no data";
                            case "empty":
                                return "–";
                            default:
                                return "";
                            }
                        }
                    }
                }
            }
        }
    }

    VsText {
        objectName: "emptyText"
        anchors.centerIn: parent
        visible: !page.ready || list.count === 0
        color: Theme.textSecondary
        text: page.ready ? "No cups yet" : page.monitor.status
    }

    VsButton {
        id: freezeButton

        objectName: "freezeButton"
        anchors {
            right: parent.right
            bottom: parent.bottom
            margins: Theme.padding
        }
        enabled: page.ready
        primary: page.monitor.frozen
        text: page.monitor.frozen ? "Back to live" : "Freeze"
        onClicked: page.monitor.toggleFreeze()
    }
}
