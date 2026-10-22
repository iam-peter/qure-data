// [stepgrid mask1 mask2 ...]: a 16-step sequencer grid, one row per argument.
//
// What this node shows:
//  * State lives in the box's own arguments: each row is a 16-bit mask,
//    written back with node.setArg(), so it is saved with the patch.
//  * nodes.json sets "loadArgs": true – when the patch loads, the engine
//    object sends the masks out of the outlet by itself. The patch gets
//    its pattern even if this node is never drawn.
//  * Timing stays in the engine: the patch's [metro] decides when a step
//    plays; the grid only shows the playhead it receives on its inlet.
//
// Inlet:  float = playhead step, "clear", "random"
// Outlet: list of row masks, whenever the pattern changes
import QtQuick
import Qure

Item {
    id: root
    required property NodeHandle node

    readonly property int steps: 16
    readonly property var masks: node.args.map(v => typeof v === "number" ? v : 0)
    readonly property int rows: Math.max(1, masks.length)
    readonly property real cellW: width / steps
    readonly property real cellH: height / rows
    readonly property var colors: ["#ff6b4a", "#ffb02e", "#3ec7a8", "#5c8dff", "#c86bff", "#ff5c9a"]
    property int playhead: -1

    function isOn(row, step) { return ((masks[row] >> step) & 1) === 1 }

    function apply(newMasks) {
        for (let r = 0; r < newMasks.length; ++r)
            if (newMasks[r] !== masks[r])
                node.setArg(r, newMasks[r])
        node.output(0, newMasks)
    }

    function setCell(row, step, on) {
        if (isOn(row, step) === on)
            return
        const m = masks.slice()
        m[row] = on ? (m[row] | (1 << step)) : (m[row] & ~(1 << step))
        apply(m)
    }

    Connections {
        target: root.node
        function onReceived(inlet, selector, args) {
            if (selector === "float")
                root.playhead = ((args[0] % root.steps) + root.steps) % root.steps
            else if (selector === "clear")
                root.apply(root.masks.map(() => 0))
            else if (selector === "random") // sparse: two random words ANDed
                root.apply(root.masks.map(() => (Math.random() * 65536 & Math.random() * 65536) | 0))
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: Qure.theme.dark ? "#16181b" : "#2a2d33"
    }

    Repeater {
        model: root.rows * root.steps
        Rectangle {
            id: cell
            required property int index
            readonly property int row: Math.floor(index / root.steps)
            readonly property int step: index % root.steps
            readonly property bool on: root.isOn(row, step)
            readonly property bool current: step === root.playhead
            readonly property color tint: root.colors[row % root.colors.length]

            x: step * root.cellW + 1.5
            y: row * root.cellH + 1.5
            width: root.cellW - 3
            height: root.cellH - 3
            radius: 3
            // Beats (every 4 steps) are a little brighter, the playhead more.
            color: on ? tint : (current ? "#5a606b" : (Math.floor(step / 4) % 2 === 0 ? "#3a3e46" : "#32363d"))
            scale: on && current ? 1.12 : 1.0
            Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }

            // A soft glow when a lit cell is played.
            Rectangle {
                anchors.centerIn: parent
                width: parent.width + 6
                height: parent.height + 6
                radius: 5
                color: "transparent"
                border.color: cell.tint
                border.width: 2
                opacity: cell.on && cell.current ? 0.7 : 0
                Behavior on opacity { NumberAnimation { duration: 160 } }
            }
        }
    }

    // Click toggles; dragging paints with the value the first cell got.
    MouseArea {
        anchors.fill: parent
        property bool paintValue: true
        function cellAt(m) {
            return {
                row: Math.max(0, Math.min(root.rows - 1, Math.floor(m.y / root.cellH))),
                step: Math.max(0, Math.min(root.steps - 1, Math.floor(m.x / root.cellW)))
            }
        }
        onPressed: (m) => {
            const c = cellAt(m)
            paintValue = !root.isOn(c.row, c.step)
            root.setCell(c.row, c.step, paintValue)
        }
        onPositionChanged: (m) => {
            const c = cellAt(m)
            root.setCell(c.row, c.step, paintValue)
        }
    }
}
