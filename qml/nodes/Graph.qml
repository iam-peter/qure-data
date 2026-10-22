// A graph holding an array (`#X array` inside a graph-on-parent canvas).
// Draws the first array; values update live as the engine changes them.
import QtQuick
import Qure

Item {
    id: root
    required property NodeHandle node
    readonly property var info: node.info

    Rectangle {
        anchors.fill: parent
        color: Qure.theme.boxFill
        border.color: Qure.theme.boxBorder
    }
    WaveView {
        anchors.fill: parent
        anchors.margins: 1
        samples: root.node.samples
        color: Qure.theme.text
        lineWidth: 1.2
        // Pd's y1 is the value at the top, y2 at the bottom.
        maximum: root.info.y1 !== undefined ? root.info.y1 : 1
        minimum: root.info.y2 !== undefined ? root.info.y2 : -1
    }
    Text {
        x: 3
        y: 2
        text: root.info.array || ""
        font.family: "DejaVu Sans Mono"
        font.pixelSize: 11
        color: Qure.theme.commentText
    }
}
