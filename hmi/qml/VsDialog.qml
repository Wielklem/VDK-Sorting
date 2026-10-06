import QtQuick
import QtQuick.Controls.Basic

Dialog {
    id: control

    anchors.centerIn: Overlay.overlay
    modal: true
    padding: Theme.padding
    standardButtons: Dialog.Ok | Dialog.Cancel

    Overlay.modal: Rectangle {
        color: "#99000000"
    }

    background: Rectangle {
        radius: Theme.radius
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: Theme.border
    }

    header: VsText {
        text: control.title
        variant: VsText.Title
        padding: Theme.padding
    }

    footer: DialogButtonBox {
        alignment: Qt.AlignRight
        spacing: Theme.spacing
        padding: Theme.padding
        background: Item {}
        delegate: VsButton {
            primary: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
        }
    }
}
