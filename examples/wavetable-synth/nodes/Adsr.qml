// [adsr attack decay sustain release]: an envelope you edit by dragging.
//
// What this node shows:
//  * Parameters live in the box's arguments (node.setArg), so they are
//    saved with the patch.
//  * nodes.json sets "loadArgs": true: on load the engine object sends
//    "A D S R" out by itself, so the patch is configured even if this
//    node is never drawn. The envelope itself runs in the engine
//    ([vline~]) – sample accurate, independent of the GUI.
//  * QtQuick Shapes for the curve, plain Items as drag handles.
//
// Inlet:  velocity / gate (> 0 lights the gate LED)
// Outlet: "A D S R" (ms, ms, 0..1, ms) whenever it changes
import QtQuick
import QtQuick.Shapes
import Qure

Item {
    id: root
    required property NodeHandle node

    function arg(i, fallback) { return typeof node.args[i] === "number" ? node.args[i] : fallback }
    readonly property real attack: arg(0, 10)
    readonly property real decay: arg(1, 200)
    readonly property real sustain: arg(2, 0.6)
    readonly property real release: arg(3, 400)
    property bool gate: false

    // Each time segment gets a quarter of the width; square-root scaling
    // gives fine control over short times. The third quarter is the
    // sustain plateau.
    readonly property real maxMs: 3000
    readonly property real seg: width / 4
    readonly property real topY: 14
    readonly property real bottomY: height - 16
    function xOf(ms) { return seg * Math.sqrt(Math.max(0, ms) / maxMs) }
    function msOf(px) { return maxMs * Math.pow(Math.max(0, Math.min(seg, px)) / seg, 2) }
    function yOf(level) { return bottomY - level * (bottomY - topY) }

    readonly property point pA: Qt.point(xOf(attack), yOf(1))
    readonly property point pD: Qt.point(pA.x + xOf(decay), yOf(sustain))
    readonly property point pS: Qt.point(pD.x + seg, yOf(sustain))
    readonly property point pR: Qt.point(pS.x + xOf(release), yOf(0))

    function update(a, d, s, r) {
        const v = [Math.round(a), Math.round(d), Math.round(s * 100) / 100, Math.round(r)]
        const old = [attack, decay, sustain, release]
        for (let i = 0; i < 4; ++i)
            if (v[i] !== old[i])
                node.setArg(i, v[i])
        node.output(0, v)
    }

    Connections {
        target: root.node
        function onReceived(inlet, selector, args) {
            if (selector === "float" || selector === "list")
                root.gate = args.length > 0 && args[args.length > 1 ? 1 : 0] > 0
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: Qure.theme.dark ? "#14161a" : "#fbfbf9"
        border.color: Qure.theme.boxBorder
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: Qure.theme.selection
            strokeWidth: 2
            fillColor: Qt.rgba(Qure.theme.selection.r, Qure.theme.selection.g, Qure.theme.selection.b, root.gate ? 0.3 : 0.15)
            startX: 0; startY: root.bottomY
            PathLine { x: root.pA.x; y: root.pA.y }
            PathLine { x: root.pD.x; y: root.pD.y }
            PathLine { x: root.pS.x; y: root.pS.y }
            PathLine { x: root.pR.x; y: root.pR.y }
            PathLine { x: 0; y: root.bottomY }
        }
    }

    // Drag handles: attack (x), decay (x) + sustain (y), release (x).
    component Handle: Rectangle {
        id: h
        property point at
        signal moved(real dx, real dy)
        x: at.x - width / 2
        y: at.y - height / 2
        width: 12
        height: 12
        radius: 6
        color: drag.pressed ? Qure.theme.selection : Qure.theme.boxFill
        border.color: Qure.theme.selection
        border.width: 2
        MouseArea {
            id: drag
            anchors.fill: parent
            anchors.margins: -6
            property point start
            onPressed: (m) => start = mapToItem(h.parent, m.x, m.y)
            onPositionChanged: (m) => {
                const p = mapToItem(h.parent, m.x, m.y)
                h.moved(p.x - start.x, p.y - start.y)
                start = p
            }
        }
    }

    Handle {
        at: root.pA
        onMoved: (dx, dy) => root.update(root.msOf(root.xOf(root.attack) + dx), root.decay, root.sustain, root.release)
    }
    Handle {
        at: root.pD
        onMoved: (dx, dy) => root.update(root.attack, root.msOf(root.xOf(root.decay) + dx),
                                          Math.max(0, Math.min(1, root.sustain - dy / (root.bottomY - root.topY))),
                                          root.release)
    }
    Handle {
        at: root.pR
        onMoved: (dx, dy) => root.update(root.attack, root.decay, root.sustain, root.msOf(root.xOf(root.release) + dx))
    }

    Row {
        x: 6
        y: root.height - 14
        spacing: 10
        Repeater {
            model: [["A", root.attack + "ms"], ["D", root.decay + "ms"], ["S", root.sustain.toFixed(2)], ["R", root.release + "ms"]]
            Text {
                required property var modelData
                text: modelData[0] + " " + modelData[1]
                font.pixelSize: 10
                font.family: "JetBrains Mono"
                color: Qure.theme.commentText
            }
        }
    }
    // Gate LED
    Rectangle {
        x: root.width - 14
        y: 6
        width: 8
        height: 8
        radius: 4
        color: root.gate ? "#3ec7a8" : Qure.theme.grid
    }
}
