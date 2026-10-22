// [wavedraw arrayname]: draw one cycle of a waveform with the mouse.
//
// What this node shows:
//  * A front end can work on engine data directly: Qure.readArray(name)
//    returns the array's contents, Qure.writeArray(name, offset, values)
//    changes it (through the engine, like a "; name offset values" message).
//  * The array is the single source of truth – it is saved with the patch
//    and the node redraws from it, so presets or other objects writing the
//    array show up here too.
//
// The array holds 32 points plus the 3 guard points [tabosc4~] needs:
//   [p31, p0 ... p31, p0, p1]
// Inlet: "sine", "saw", "square", "triangle", "random", "smooth"
import QtQuick
import Qure

Item {
    id: root
    required property NodeHandle node

    readonly property string table: node.args.length > 0 ? String(node.args[0]) : "wavetable"
    readonly property int points: 32
    property var values: []
    property bool drawing: false
    property int lastIndex: -1

    function load() {
        const a = Array.from(Qure.readArray(table))
        if (a.length >= points + 3)
            values = a.slice(1, points + 1)
    }

    function store(v) {
        values = v
        Qure.writeArray(table, 0, [v[points - 1]].concat(v, [v[0], v[1]]))
    }

    function preset(name) {
        const v = []
        for (let i = 0; i < points; ++i) {
            const ph = i / points
            switch (name) {
            case "saw": v.push(1 - 2 * ph); break
            case "square": v.push(ph < 0.5 ? 0.8 : -0.8); break
            case "triangle": v.push(ph < 0.5 ? 4 * ph - 1 : 3 - 4 * ph); break
            case "random": v.push(Math.random() * 2 - 1); break
            case "smooth": { // a soft filter over the current shape
                const n = values.length === points ? values : v
                v.push(0.25 * (n[(i + points - 1) % points] || 0) + 0.5 * (n[i] || 0) + 0.25 * (n[(i + 1) % points] || 0))
                break
            }
            default: v.push(Math.sin(2 * Math.PI * ph))
            }
        }
        store(v)
    }

    Connections {
        target: root.node
        function onReceived(inlet, selector, args) { root.preset(selector) }
    }

    Component.onCompleted: load()
    // Follow changes made elsewhere (only while not drawing).
    Timer {
        interval: 300
        repeat: true
        running: !root.drawing
        onTriggered: root.load()
    }

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: Qure.theme.dark ? "#14161a" : "#fbfbf9"
        border.color: Qure.theme.boxBorder
    }
    // Zero line and quarter guides.
    Rectangle { y: root.height / 2; width: root.width; height: 1; color: Qure.theme.grid }
    Repeater {
        model: 3
        Rectangle {
            required property int index
            x: root.width * (index + 1) / 4
            width: 1
            height: root.height
            color: Qure.theme.grid
        }
    }
    // Bars for the points, the interpolated curve on top.
    Repeater {
        model: root.values.length
        Rectangle {
            required property int index
            readonly property real v: root.values[index]
            x: index * root.width / root.points + 1
            width: root.width / root.points - 2
            y: v >= 0 ? root.height / 2 * (1 - v) : root.height / 2
            height: Math.abs(v) * root.height / 2
            color: Qure.theme.selection
            opacity: 0.22
            radius: 1
        }
    }
    WaveView {
        anchors.fill: parent
        samples: root.values.concat(root.values.length ? [root.values[0]] : [])
        color: Qure.theme.selection
        lineWidth: 2
        minimum: -1
        maximum: 1
    }
    Text {
        x: 6
        y: 4
        text: root.table
        font.pixelSize: 10
        color: Qure.theme.commentText
    }

    MouseArea {
        anchors.fill: parent
        function paint(m, first) {
            const i = Math.max(0, Math.min(root.points - 1, Math.floor(m.x / root.width * root.points)))
            const v = Math.max(-1, Math.min(1, 1 - 2 * m.y / root.height))
            const next = root.values.length === root.points ? root.values.slice() : new Array(root.points).fill(0)
            // Fill the gap between two mouse events so fast strokes stay continuous.
            const from = first || root.lastIndex < 0 ? i : root.lastIndex
            const steps = Math.abs(i - from)
            for (let k = 0; k <= steps; ++k) {
                const j = from + Math.sign(i - from) * k
                next[j] = steps === 0 ? v : next[from] + (v - next[from]) * k / steps
            }
            root.lastIndex = i
            root.store(next)
        }
        onPressed: (m) => { root.drawing = true; paint(m, true) }
        onPositionChanged: (m) => paint(m, false)
        onReleased: { root.drawing = false; root.lastIndex = -1 }
    }
}
