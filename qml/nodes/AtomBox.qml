// Pd's atom boxes: floatatom, symbolatom and listbox.
// Arguments: width min max label_pos label receive send
import QtQuick
import Qure
import "Iem.js" as Iem

Item {
    id: root
    required property NodeHandle node
    readonly property var a: node.args
    readonly property bool isFloat: node.className === "@floatatom"
    readonly property string labelText: Iem.label(a[4])

    // Background and outline come from the canvas renderer (atom shape).
    NumberDrag {
        anchors.fill: parent
        visible: root.isFloat
        node: root.node
        value: typeof root.node.value === "number" ? root.node.value : 0
        minimum: Iem.num(root.a[1], 0)
        maximum: Iem.num(root.a[2], 0)
        digits: Math.max(1, Iem.num(root.a[0], 5) || 5)
    }

    Text {
        anchors.fill: parent
        visible: !root.isFloat && !entry.visible
        leftPadding: 4
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        font.family: "JetBrains Mono"
        font.pixelSize: 12
        color: Qure.theme.text
        text: {
            const v = root.node.value
            if (v === undefined) return root.node.className === "@listbox" ? "" : "symbol"
            return Array.isArray(v) ? v.join(" ") : String(v)
        }
    }
    TextInput {
        id: entry
        anchors.fill: parent
        visible: false
        leftPadding: 4
        verticalAlignment: Text.AlignVCenter
        font.family: "JetBrains Mono"
        font.pixelSize: 12
        color: Qure.theme.selection
        onAccepted: {
            if (root.node.className === "@listbox")
                root.node.send(text.split(/\s+/).filter(s => s.length).map(s => isNaN(Number(s)) ? s : Number(s)))
            else
                root.node.sendMessage("symbol", [text])
            visible = false
        }
        onActiveFocusChanged: if (!activeFocus) visible = false
        Keys.onEscapePressed: visible = false
    }
    MouseArea {
        anchors.fill: parent
        enabled: !root.isFloat
        onDoubleClicked: {
            entry.text = ""
            entry.visible = true
            entry.forceActiveFocus()
        }
    }

    // Label: 0 left, 1 right, 2 top, 3 bottom (Pd's label_pos).
    Text {
        visible: root.labelText.length > 0
        text: root.labelText
        font.family: "JetBrains Mono"
        font.pixelSize: 12
        color: Qure.theme.text
        readonly property int where: Iem.num(root.a[3], 0)
        x: where === 0 ? -width - 4 : where === 1 ? root.width + 4 : 0
        y: where === 2 ? -height - 2 : where === 3 ? root.height + 2 : (root.height - height) / 2
    }
}
