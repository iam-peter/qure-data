// [hsl] / [vsl]: width height min max log init send receive label x y font fontsize bg fg labelcolor value steady
import QtQuick
import Qure
import "Iem.js" as Iem

Item {
    id: root
    required property NodeHandle node
    readonly property var a: node.args
    readonly property bool vertical: node.className === "vsl" || node.className === "vslider"
    readonly property real minimum: Iem.num(a[2], 0)
    readonly property real maximum: Iem.num(a[3], 127)
    readonly property bool logScale: Iem.num(a[4], 0) !== 0 && minimum > 0 && maximum > 0
    readonly property real value: typeof node.value === "number" ? node.value : minimum
    readonly property real span: vertical ? height : width

    // 0..1 position of a value and back, linear or logarithmic.
    function toPos(v) {
        if (maximum === minimum) return 0
        if (logScale) return Math.log(v / minimum) / Math.log(maximum / minimum)
        return (v - minimum) / (maximum - minimum)
    }
    function fromPos(p) {
        p = Math.max(0, Math.min(1, p))
        if (logScale) return minimum * Math.pow(maximum / minimum, p)
        return minimum + p * (maximum - minimum)
    }

    Rectangle {
        anchors.fill: parent
        color: Iem.bg(root.a[13], Qure.theme)
        border.color: Qure.theme.boxBorder
        border.width: 1
    }
    // Filled part.
    Rectangle {
        readonly property real p: Math.max(0, Math.min(1, root.toPos(root.value)))
        x: 1
        y: root.vertical ? (root.height - 1) - p * (root.height - 2) : 1
        width: root.vertical ? root.width - 2 : p * (root.width - 2)
        height: root.vertical ? p * (root.height - 2) : root.height - 2
        color: Iem.color(root.a[14], Qure.theme.selection, ["#000000"])
        opacity: 0.25
    }
    // Thumb.
    Rectangle {
        readonly property real p: Math.max(0, Math.min(1, root.toPos(root.value)))
        x: root.vertical ? 1 : 1 + p * (root.width - 5)
        y: root.vertical ? root.height - 4 - p * (root.height - 5) : 1
        width: root.vertical ? root.width - 2 : 3
        height: root.vertical ? 3 : root.height - 2
        color: Iem.color(root.a[14], Qure.theme.selection, ["#000000"])
    }
    IemLabel { labelArg: root.a[8]; dx: root.a[9]; dy: root.a[10]; size: root.a[12]; colorArg: root.a[15] }

    MouseArea {
        anchors.fill: parent
        property real startPos: 0
        property real startMouse: 0
        // Pd: jump to the click unless "steady on click"; shift = fine.
        onPressed: (m) => {
            const steady = Iem.num(root.a[17], 1) !== 0
            const pos = root.vertical ? 1 - m.y / root.height : m.x / root.width
            startPos = steady ? root.toPos(root.value) : pos
            startMouse = root.vertical ? -m.y : m.x
            if (!steady)
                root.node.send(root.fromPos(pos))
        }
        onPositionChanged: (m) => {
            const fine = (m.modifiers & Qt.ShiftModifier) ? 0.01 : 1
            const d = ((root.vertical ? -m.y : m.x) - startMouse) / root.span * fine
            root.node.send(root.fromPos(startPos + d))
            if (fine !== 1) {
                startPos += d
                startMouse = root.vertical ? -m.y : m.x
            }
        }
    }
}
