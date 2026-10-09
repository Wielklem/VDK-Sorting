
import QtQuick
import QtQuick.Controls.Basic
import VsortHmi

// G140.10 Product monitor (P80.110, P60.15): the last cups of one lane, newest on top. Per sensor a
// photo column ("photo OK" or red "no data"; empty while the cup has not reached the sensor) plus
// its measurement columns ("–" until the analysis delivered a value; "present"/"empty" for a
// presence column). Columns follow the machine config.
Rectangle {
    id: page

    required property var monitor // ProductMonitorModel

    readonly property int cupColumnWidth: 90
    readonly property int cellHeight: Theme.tableRowHeight
    readonly property real cellWidth: Math.max(90, (list.width - page.cupColumnWidth) / Math.max(1, page.monitor.columns.length))
    readonly property bool ready: page.monitor.status === ""
    readonly property int scrollBarWidth: 14
    // Whole rows that fit in the list; the model gives exactly these rows (from firstRow).
    readonly property int visibleRows: Math.max(1, Math.floor(list.height / page.cellHeight))

    Binding {
        target: page.monitor
        property: "windowSize"
        value: page.visibleRows
    }

    // Wheel anywhere on the page: 3 rows per notch.
    WheelHandler {
        target: null
        onWheel: event => page.monitor.firstRow = page.monitor.firstRow - Math.round(event.angleDelta.y / 40)
    }

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

            VsText {
                objectName: "rangeText"
                anchors.verticalCenter: parent.verticalCenter
                visible: page.ready && page.monitor.rowCount > 0
                color: Theme.textSecondary
                text: "Rows %1–%2 of %3".arg(page.monitor.firstRow + 1).arg(Math.min(page.monitor.firstRow + page.visibleRows, page.monitor.rowCount)).arg(page.monitor.rowCount)
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
            rightMargin: Theme.padding + page.scrollBarWidth + Theme.spacing
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
        // No flicking: the scroll bar or the wheel picks the rows and the model gives exactly
        // those, so new cups only change cells and the view never moves.
        interactive: false
        visible: page.ready
        model: page.monitor.windowRows

        delegate: Row {
            id: rowItem

            required property int index
            required property var cupId
            required property var cells
            required property var values

            // Cup with product (presence column "present"): lighter row. Empty or not measured: dark.
            readonly property bool filled: rowItem.cells.indexOf("present") >= 0
            readonly property color rowColor: rowItem.filled
                ? (rowItem.index % 2 === 0 ? Theme.rowFilled : Qt.lighter(Theme.rowFilled, 1.1))
                : (rowItem.index % 2 === 0 ? Theme.surface : Theme.surfaceRaised)

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
                    variant: VsText.Caption
                    text: String(rowItem.cupId)
                }
            }

            Repeater {
                model: rowItem.cells
                delegate: Rectangle {
                    id: cell

                    required property int index
                    required property string modelData
                    readonly property bool measured: cell.modelData === "value" || cell.modelData === "present" || cell.modelData === "absent"

                    objectName: cell.modelData === "nodata" ? "noDataCell" : (cell.modelData === "ok" ? "okCell" : (cell.measured ? "valueCell" : "cell"))
                    width: page.cellWidth
                    height: page.cellHeight
                    color: cell.modelData === "nodata" ? Theme.error : rowItem.rowColor
                    border.width: Theme.borderWidth
                    border.color: Theme.background

                    VsText {
                        anchors.centerIn: parent
                        wrapMode: Text.NoWrap
                        variant: VsText.Caption
                        color: {
                            switch (cell.modelData) {
                            case "nodata":
                                return Theme.textOnAccent;
                            case "ok":
                            case "present":
                                return Theme.ok;
                            case "value":
                                return Theme.textPrimary;
                            case "absent":
                                return Theme.textSecondary;
                            default:
                                return Theme.textDisabled;
                            }
                        }
                        text: {
                            switch (cell.modelData) {
                            case "ok":
                                return "photo OK";
                            case "nodata":
                                return "no data";
                            case "empty":
                                return "–";
                            case "value":
                            case "present":
                            case "absent":
                                return rowItem.values[cell.index];
                            default:
                                return "";
                            }
                        }
                    }
                }
            }
        }
    }

    // Picks which rows the list shows; the handle size is the visible part of all rows.
    ScrollBar {
        id: rowBar

        readonly property int total: page.monitor.rowCount

        objectName: "rowBar"
        anchors {
            top: list.top
            bottom: list.bottom
            right: parent.right
            rightMargin: Theme.padding
        }
        width: page.scrollBarWidth
        visible: page.ready
        orientation: Qt.Vertical
        policy: ScrollBar.AlwaysOn
        size: rowBar.total > 0 ? Math.min(1, page.visibleRows / rowBar.total) : 1
        position: rowBar.total > 0 ? page.monitor.firstRow / rowBar.total : 0
        stepSize: rowBar.total > 0 ? 1 / rowBar.total : 0
        onPositionChanged: if (rowBar.pressed)
            page.monitor.firstRow = Math.round(rowBar.position * rowBar.total)

        contentItem: Rectangle {
            implicitWidth: page.scrollBarWidth
            radius: page.scrollBarWidth / 2
            color: rowBar.pressed ? Theme.accent : Theme.textSecondary
        }
        background: Rectangle {
            color: Theme.surface
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
