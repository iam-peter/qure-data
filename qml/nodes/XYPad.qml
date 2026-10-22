// [xypad size]: drag a point; outputs "x y" (0..1) from its outlet.
// Like Keyboard.qml, this node is pure QML – see nodes.json.
import QtQuick
import Qure

Item {
    id: root
    required property NodeHandle node
    property real px: 0.5
    property real py: 0.5

    // A list sent to the inlet moves the point without output.
    Connections {
        target: root.node
        function onReceived(inlet, selector, args) {
            if (selector === "list" && args.length >= 2) {
                root.px = Math.max(0, Math.min(1, args[0]))
                root.py = Math.max(0, Math.min(1, args[1]))
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Qure.theme.boxFill
        border.color: Qure.theme.boxBorder
        radius: 3
    }
    Rectangle { x: root.px * root.width; width: 1; height: root.height; color: Qure.theme.grid }
    Rectangle { y: (1 - root.py) * root.height; height: 1; width: root.width; color: Qure.theme.grid }
    Rectangle {
        width: 12
        height: 12
        radius: 6
        x: root.px * root.width - 6
        y: (1 - root.py) * root.height - 6
        color: Qure.theme.selection
    }
    MouseArea {
        anchors.fill: parent
        function move(m) {
            root.px = Math.max(0, Math.min(1, m.x / root.width))
            root.py = Math.max(0, Math.min(1, 1 - m.y / root.height))
            root.node.output(0, [root.px, root.py])
        }
        onPressed: (m) => move(m)
        onPositionChanged: (m) => move(m)
    }
}
