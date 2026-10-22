// [kbd octaves lowest]: a piano keyboard defined entirely in QML.
//
// There is no C++ for this node: nodes.json declares 1 inlet and 1 outlet,
// and the engine creates a generic object that forwards messages here
// (node.received) and sends whatever we pass to node.output().
//
//   outlet 0: "note velocity" lists (velocity 0 = note off)
//   inlet 0:  "note velocity" lists light up keys (e.g. from [notein])
import QtQuick
import Qure

Item {
    id: root
    required property NodeHandle node
    readonly property int octaves: Math.max(1, Math.min(8, typeof node.args[0] === "number" ? node.args[0] : 3))
    readonly property int lowest: typeof node.args[1] === "number" ? node.args[1] : 48
    readonly property real whiteWidth: width / (octaves * 7)
    readonly property var whiteSteps: [0, 2, 4, 5, 7, 9, 11]
    readonly property var blackSteps: [1, 3, -1, 6, 8, 10, -1]
    property var lit: ({})      // note -> true, from the inlet
    property int pressed: -1    // note held with the mouse

    function setLit(note, on) {
        const l = Object.assign({}, lit)
        if (on) l[note] = true
        else delete l[note]
        lit = l
    }

    Connections {
        target: root.node
        function onReceived(inlet, selector, args) {
            if (selector === "list" && args.length >= 2)
                root.setLit(args[0], args[1] > 0)
            else if (selector === "float")
                root.setLit(args[0], true)
            else if (selector === "clear")
                root.lit = {}
        }
    }

    function play(note) {
        if (note === pressed)
            return
        if (pressed >= 0)
            node.output(0, [pressed, 0])
        pressed = note
        if (note >= 0)
            node.output(0, [note, 100])
    }

    function noteAt(px, py) {
        const octave = Math.floor(px / (whiteWidth * 7))
        const inOct = px - octave * whiteWidth * 7
        if (py < height * 0.6) {
            for (let i = 0; i < 7; ++i) {
                if (blackSteps[i] < 0) continue
                const bx = (i + 1) * whiteWidth - whiteWidth * 0.3
                if (inOct >= bx && inOct < bx + whiteWidth * 0.6)
                    return lowest + octave * 12 + blackSteps[i]
            }
        }
        const w = Math.max(0, Math.min(6, Math.floor(inOct / whiteWidth)))
        return lowest + octave * 12 + whiteSteps[w]
    }

    Repeater {
        model: root.octaves * 7
        Rectangle {
            required property int index
            readonly property int note: root.lowest + Math.floor(index / 7) * 12 + root.whiteSteps[index % 7]
            x: index * root.whiteWidth
            width: root.whiteWidth + 1
            height: root.height
            color: note === root.pressed || root.lit[note] ? Qure.theme.selection : (Qure.theme.dark ? "#e8e8e8" : "white")
            border.color: Qure.theme.boxBorder
        }
    }
    Repeater {
        model: root.octaves * 7
        Rectangle {
            required property int index
            readonly property int step: root.blackSteps[index % 7]
            readonly property int note: root.lowest + Math.floor(index / 7) * 12 + step
            visible: step >= 0
            x: (index + 1) * root.whiteWidth - root.whiteWidth * 0.3
            width: root.whiteWidth * 0.6
            height: root.height * 0.6
            radius: 1
            color: note === root.pressed || root.lit[note] ? Qure.theme.selection : "#222"
        }
    }
    MouseArea {
        anchors.fill: parent
        onPressed: (m) => root.play(root.noteAt(m.x, m.y))
        onPositionChanged: (m) => { if (pressed) root.play(root.noteAt(Math.max(0, Math.min(root.width - 1, m.x)), m.y)) }
        onReleased: root.play(-1)
        onCanceled: root.play(-1)
    }
}
