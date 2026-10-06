import QtQuick
import QtQuick.Controls.Basic
import Qt.labs.qmlmodels

ApplicationWindow {
    id: win

    property bool selfTest: false

    width: 1280
    height: 800
    visible: true
    title: "VDK Sorting - design system"
    color: Theme.background

    Component.onCompleted: if (selfTest)
        dialog.open()

    Column {
        anchors.fill: parent
        anchors.margins: Theme.padding
        spacing: Theme.padding

        VsText {
            text: "Design system"
            variant: VsText.Heading
        }
        VsText {
            text: "Title"
            variant: VsText.Title
        }
        VsText {
            text: "Body text"
        }
        VsText {
            text: "Caption text"
            variant: VsText.Caption
        }

        Row {
            spacing: Theme.spacing
            Repeater {
                model: [Theme.accent, Theme.ok, Theme.warn, Theme.error, Theme.surfaceRaised, Theme.border]
                delegate: Rectangle {
                    required property color modelData
                    width: 80
                    height: 40
                    radius: Theme.radius
                    color: modelData
                }
            }
        }

        Row {
            spacing: Theme.spacing
            VsButton {
                text: "Default"
            }
            VsButton {
                text: "Primary"
                primary: true
            }
            VsButton {
                text: "Disabled"
                enabled: false
            }
            VsButton {
                text: "Open dialog"
                onClicked: dialog.open()
            }
        }

        Row {
            spacing: Theme.spacing
            VsNumberInput {
                value: 5
                from: 0.1
                to: 100
                decimals: 1
                unit: "ms"
            }
            VsNumberInput {
                value: 12
                from: 0
                to: 24
                unit: "dB"
            }
        }

        VsTable {
            width: parent.width
            height: 240
            headers: ["Camera", "Serial", "State"]
            model: TableModel {
                TableModelColumn {
                    display: "camera"
                }
                TableModelColumn {
                    display: "serial"
                }
                TableModelColumn {
                    display: "state"
                }
                rows: [
                    {
                        camera: "Lane 1",
                        serial: "FDE24010001",
                        state: "OK"
                    },
                    {
                        camera: "Lane 2",
                        serial: "FDE24010002",
                        state: "OK"
                    },
                    {
                        camera: "Lane 3",
                        serial: "FDE24010003",
                        state: "Offline"
                    }
                ]
            }
        }
    }

    VsDialog {
        id: dialog
        title: "Confirm"
        VsText {
            text: "Apply the new settings?"
        }
    }
}
