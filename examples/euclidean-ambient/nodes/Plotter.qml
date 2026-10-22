// [plotter length min max]: a scrolling history of the numbers it receives.
//
// What this node shows: a display-only node (no outlets) that keeps its own
// state in QML and draws with WaveView – the same GPU polyline item the
// built-in scope~ uses.
import QtQuick
import Qure

Item {
    id: root
    required property NodeHandle node

    function arg(i, fallback) { return typeof node.args[i] === "number" ? node.args[i] : fallback }
    readonly property int length: Math.max(8, arg(0, 200))
    readonly property real minimum: arg(1, 0)
    readonly property real maximum: arg(2, 100)
    property var history: []
    property real last: NaN

    Connections {
        target: root.node
        function onReceived(inlet, selector, args) {
            if (selector !== "float")
                return
            const h = root.history.length >= root.length ? root.history.slice(1) : root.history.slice()
            h.push(args[0])
            root.history = h
            root.last = args[0]
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: Qure.theme.dark ? "#14161a" : "#fbfbf9"
        border.color: Qure.theme.boxBorder
    }
    Repeater {
        model: 3
        Rectangle {
            required property int index
            y: root.height * (index + 1) / 4
            width: root.width
            height: 1
            color: Qure.theme.grid
        }
    }
    WaveView {
        anchors.fill: parent
        anchors.margins: 3
        // Pad on the left so new values enter from the right edge.
        samples: new Array(Math.max(0, root.length - root.history.length)).fill(root.minimum).concat(root.history)
        minimum: root.minimum
        maximum: root.maximum
        color: "#3ec7a8"
        lineWidth: 1.5
    }
    Text {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 4
        text: isNaN(root.last) ? "" : root.last.toFixed(1)
        font.pixelSize: 10
        font.family: "DejaVu Sans Mono"
        color: Qure.theme.commentText
    }
}
