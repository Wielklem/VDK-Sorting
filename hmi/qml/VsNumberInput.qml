import QtQuick
import QtQuick.Controls.Basic

// Numeric input: '.' as decimal separator, value clamped to [from, to] on commit.
TextField {
    id: control

    property real value: 0
    property real from: 0
    property real to: 100
    property int decimals: 0
    property string unit: ""

    signal edited(real newValue)

    function format(v) {
        return Number(v).toFixed(decimals);
    }

    function commit() {
        const n = parseFloat(text);
        if (!acceptableInput || isNaN(n)) {
            text = format(value);
            return;
        }
        const v = Math.min(to, Math.max(from, n));
        text = format(v);
        if (v !== value) {
            value = v;
            edited(v);
        }
    }

    text: format(value)
    onValueChanged: if (!activeFocus)
        text = format(value)
    onEditingFinished: commit()
    onActiveFocusChanged: if (!activeFocus)
        commit()

    validator: DoubleValidator {
        decimals: control.decimals
        notation: DoubleValidator.StandardNotation
        locale: "C"
    }
    inputMethodHints: Qt.ImhFormattedNumbersOnly
    selectByMouse: true
    horizontalAlignment: TextInput.AlignRight
    implicitHeight: Theme.controlHeight
    implicitWidth: 160
    font.family: Theme.fontFamily
    font.pixelSize: Theme.sizeBody
    color: enabled ? Theme.textPrimary : Theme.textDisabled
    selectionColor: Theme.accent
    leftPadding: Theme.padding
    rightPadding: Theme.padding + (unit !== "" ? unitText.implicitWidth + Theme.spacing : 0)

    Text {
        id: unitText
        visible: control.unit !== ""
        text: control.unit
        font.family: Theme.fontFamily
        font.pixelSize: Theme.sizeBody
        color: Theme.textSecondary
        anchors.right: parent.right
        anchors.rightMargin: Theme.padding
        anchors.verticalCenter: parent.verticalCenter
    }

    background: Rectangle {
        radius: Theme.radius
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: !control.acceptableInput ? Theme.error : (control.activeFocus ? Theme.accent : Theme.border)
    }
}
