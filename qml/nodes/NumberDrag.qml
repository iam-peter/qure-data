// Shared interaction for number boxes: drag vertically to change the value
// (shift: 0.01 steps), or double-click and type a number, Enter to send.
import QtQuick
import Qure

Item {
    id: root
    required property NodeHandle node
    property real value: 0
    property real minimum: 0
    property real maximum: 0
    property string display: ""
    property int digits: 5
    property color textColor: Qure.theme.text

    function clip(v) {
        return (minimum === 0 && maximum === 0) ? v : Math.max(minimum, Math.min(maximum, v))
    }
    function format(v) {
        let s = Number(v.toPrecision(6)).toString()
        if (s.length > digits) s = s.substring(0, digits - 1) + ">"
        return s
    }

    Text {
        anchors.fill: parent
        leftPadding: 4
        verticalAlignment: Text.AlignVCenter
        font.family: "DejaVu Sans Mono"
        font.pixelSize: Math.max(6, Math.min(12, root.height - 4))
        color: root.textColor
        text: entry.visible ? "" : (root.display !== "" ? root.display : root.format(root.value))
    }

    TextInput {
        id: entry
        anchors.fill: parent
        leftPadding: 4
        verticalAlignment: Text.AlignVCenter
        font.family: "DejaVu Sans Mono"
        font.pixelSize: Math.max(6, Math.min(12, root.height - 4))
        color: Qure.theme.selection
        visible: false
        validator: DoubleValidator {}
        onAccepted: {
            root.node.send(root.clip(Number(text)))
            visible = false
        }
        onActiveFocusChanged: if (!activeFocus) visible = false
        Keys.onEscapePressed: visible = false
    }

    MouseArea {
        anchors.fill: parent
        property real startY: 0
        property real startValue: 0
        onPressed: (m) => { startY = m.y; startValue = root.value }
        onPositionChanged: (m) => {
            const step = (m.modifiers & Qt.ShiftModifier) ? 0.01 : 1
            const v = root.clip(startValue + Math.round(startY - m.y) * step)
            if (v !== root.value)
                root.node.send(v)
        }
        onDoubleClicked: {
            entry.text = ""
            entry.visible = true
            entry.forceActiveFocus()
        }
    }
}
