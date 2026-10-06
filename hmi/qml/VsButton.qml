import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control

    property bool primary: false

    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(100, contentItem.implicitWidth + 2 * Theme.padding)
    font.family: Theme.fontFamily
    font.pixelSize: Theme.sizeBody

    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? Theme.textDisabled : (control.primary ? Theme.textOnAccent : Theme.textPrimary)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radius
        border.width: control.primary ? 0 : Theme.borderWidth
        border.color: control.visualFocus ? Theme.accent : Theme.border
        color: {
            if (!control.enabled)
                return Theme.surface;
            if (control.primary)
                return control.down ? Theme.accentPressed : Theme.accent;
            return control.down ? Theme.border : Theme.surfaceRaised;
        }
    }
}
