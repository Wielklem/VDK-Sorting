import QtQuick
import QtQuick.Controls.Basic

// Read-only table. `model` must provide the `display` role (e.g. TableModel
// or a C++ QAbstractTableModel). `headers` = column titles; columns share the width.
Rectangle {
    id: root

    property var model: null
    property var headers: []
    property int currentRow: -1
    readonly property int rowHeight: Theme.controlHeight
    readonly property real columnWidth: headers.length > 0 ? width / headers.length : 0

    color: Theme.surface
    border.width: Theme.borderWidth
    border.color: Theme.border
    radius: Theme.radius
    clip: true

    onColumnWidthChanged: table.forceLayout()

    Row {
        id: headerRow
        Repeater {
            model: root.headers
            delegate: Rectangle {
                id: headerCell
                required property string modelData
                width: root.columnWidth
                height: root.rowHeight
                color: Theme.surfaceRaised
                VsText {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                    variant: VsText.Caption
                    font.weight: Font.DemiBold
                    text: headerCell.modelData
                }
            }
        }
    }

    TableView {
        id: table
        anchors.top: headerRow.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.model
        columnWidthProvider: function () {
            return root.columnWidth;
        }
        rowHeightProvider: function () {
            return root.rowHeight;
        }

        delegate: Rectangle {
            id: cell
            required property int row
            required property var display
            color: row === root.currentRow ? Theme.accent : (row % 2 === 0 ? Theme.surface : Theme.surfaceRaised)
            VsText {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
                color: cell.row === root.currentRow ? Theme.textOnAccent : Theme.textPrimary
                text: String(cell.display)
            }
            TapHandler {
                onTapped: root.currentRow = cell.row
            }
        }
    }
}
