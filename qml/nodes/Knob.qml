// [knob size min max init send receive]: drag up/down, shift for fine steps.
import QtQuick
import QtQuick.Shapes
import Qure
import "Iem.js" as Iem

Item {
    id: root
    required property NodeHandle node
    readonly property var a: node.args
    readonly property real minimum: Iem.num(a[1], 0)
    readonly property real maximum: Iem.num(a[2], 127)
    readonly property real value: typeof node.value === "number" ? node.value : minimum
    readonly property real pos: maximum === minimum ? 0 : Math.max(0, Math.min(1, (value - minimum) / (maximum - minimum)))
    readonly property real sweep: 270

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Qure.theme.boxFill
        border.color: Qure.theme.boxBorder
    }
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: Qure.theme.selection
            strokeWidth: Math.max(2, root.width / 10)
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: root.width / 2 - root.width / 9
                radiusY: radiusX
                startAngle: 135
                sweepAngle: root.sweep * root.pos
            }
        }
    }
    // Pointer.
    Rectangle {
        width: Math.max(2, root.width / 14)
        height: root.height * 0.32
        radius: width / 2
        color: Qure.theme.text
        x: root.width / 2 - width / 2
        y: root.height / 2 - height
        transformOrigin: Item.Bottom
        rotation: -135 + root.sweep * root.pos
    }
    Text {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: root.height * 0.28
        text: Number(root.value.toPrecision(4)).toString()
        font.pixelSize: Math.max(6, root.height / 6)
        color: Qure.theme.commentText
    }

    MouseArea {
        anchors.fill: parent
        property real startY: 0
        property real startPos: 0
        onPressed: (m) => { startY = m.y; startPos = root.pos }
        onPositionChanged: (m) => {
            const fine = (m.modifiers & Qt.ShiftModifier) ? 0.1 : 1
            const p = Math.max(0, Math.min(1, startPos + (startY - m.y) / 150 * fine))
            root.node.send(root.minimum + p * (root.maximum - root.minimum))
        }
        onDoubleClicked: root.node.send(root.minimum)
    }
}
