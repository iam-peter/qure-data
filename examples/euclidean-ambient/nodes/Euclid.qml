// [euclid steps hits rotation hue]: a euclidean rhythm on a ring.
//
// Drag up/down: more or fewer hits. Drag left/right: rotate.
// Scroll: change the number of steps.
//
// What this node shows:
//  * Interaction that reads naturally in QML (a polygon through the hits,
//    animated playhead) and state kept in the box's arguments.
//  * The engine, not the GUI, decides when a step sounds: the node only
//    sends "steps hits rotation" (on change, and – "loadArgs" – when the
//    patch loads) and the patch's [expr] tests each step. The playhead
//    arrives on the inlet.
//
// A step i is a hit when ((i + rotation) * hits) mod steps < hits – the same
// formula the patch uses, which spreads the hits as evenly as possible.
import QtQuick
import QtQuick.Shapes
import Qure

Item {
    id: root
    required property NodeHandle node

    function arg(i, fallback) { return typeof node.args[i] === "number" ? node.args[i] : fallback }
    readonly property int steps: Math.max(2, arg(0, 16))
    readonly property int hits: Math.max(0, Math.min(steps, arg(1, 5)))
    readonly property int rot: arg(2, 0)
    readonly property color tint: Qt.hsla(arg(3, 200) / 360, 0.75, 0.6, 1)
    property int playhead: -1
    property real pulse: 0

    function isHit(i) { return ((i + rot) * hits) % steps < hits }

    function set(s, h, r) {
        s = Math.max(2, Math.min(32, s))
        h = Math.max(0, Math.min(s, h))
        r = ((r % s) + s) % s
        const v = [s, h, r]
        const old = [steps, hits, rot]
        for (let i = 0; i < 3; ++i)
            if (v[i] !== old[i])
                node.setArg(i, v[i])
        node.output(0, v)
    }

    Connections {
        target: root.node
        function onReceived(inlet, selector, args) {
            if (selector !== "float")
                return
            root.playhead = args[0]
            if (root.isHit(root.playhead))
                pulseAnim.restart()
        }
    }
    NumberAnimation { id: pulseAnim; target: root; property: "pulse"; from: 1; to: 0; duration: 400; easing.type: Easing.OutCubic }

    readonly property real cx: width / 2
    readonly property real cy: height / 2
    readonly property real radius: Math.min(width, height) / 2 - 9
    function px(i) { return cx + radius * Math.sin(2 * Math.PI * i / steps) }
    function py(i) { return cy - radius * Math.cos(2 * Math.PI * i / steps) }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Qure.theme.dark ? "#14161a" : "#fbfbf9"
        border.color: Qure.theme.boxBorder
    }

    // The polygon through all hits.
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: root.tint
            strokeWidth: 1.5
            fillColor: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.12 + 0.25 * root.pulse)
            PathPolyline {
                path: {
                    const pts = []
                    for (let i = 0; i < root.steps; ++i)
                        if (root.isHit(i))
                            pts.push(Qt.point(root.px(i), root.py(i)))
                    if (pts.length > 0)
                        pts.push(pts[0])
                    return pts
                }
            }
        }
    }

    Repeater {
        model: root.steps
        Rectangle {
            required property int index
            readonly property bool hit: root.isHit(index)
            readonly property bool current: index === root.playhead
            width: hit ? 11 : 6
            height: width
            radius: width / 2
            x: root.px(index) - width / 2
            y: root.py(index) - height / 2
            color: hit ? root.tint : Qure.theme.grid
            border.color: current ? Qure.theme.text : "transparent"
            border.width: 2
            scale: current && hit ? 1.0 + 0.6 * root.pulse : 1.0
        }
    }

    Text {
        anchors.centerIn: parent
        text: root.hits + "/" + root.steps
        font.pixelSize: Math.max(8, root.radius / 3.2)
        font.bold: true
        color: Qure.theme.text
        opacity: 0.8
    }

    MouseArea {
        anchors.fill: parent
        property point start
        property int startHits
        property int startRot
        onPressed: (m) => { start = Qt.point(m.x, m.y); startHits = root.hits; startRot = root.rot }
        onPositionChanged: (m) => {
            const h = startHits + Math.round((start.y - m.y) / 8)
            const r = startRot - Math.round((m.x - start.x) / 10)
            if (h !== root.hits || r !== root.rot)
                root.set(root.steps, h, r)
        }
        onWheel: (w) => root.set(root.steps + (w.angleDelta.y > 0 ? 1 : -1), root.hits, root.rot)
    }
}
