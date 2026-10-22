// [nbx] digits height min max log init send receive label x y font fontsize bg fg labelcolor value log_height
import QtQuick
import Qure
import "Iem.js" as Iem

Item {
    id: root
    required property NodeHandle node
    readonly property var a: node.args

    Rectangle {
        anchors.fill: parent
        color: Iem.bg(root.a[13], Qure.theme)
        border.color: Qure.theme.boxBorder
        border.width: 1
    }
    // Pd's little triangle on the left.
    Rectangle {
        width: root.height / 2 - 2
        height: width
        x: 1 - width / 2
        y: root.height / 2 - height / 2
        rotation: 45
        color: Iem.color(root.a[14], Qure.theme.text, ["#000000"])
        opacity: 0.6
        clip: false
    }
    NumberDrag {
        anchors.fill: parent
        anchors.leftMargin: root.height / 2
        node: root.node
        value: typeof root.node.value === "number" ? root.node.value : 0
        minimum: Iem.num(root.a[2], -1e37)
        maximum: Iem.num(root.a[3], 1e37)
        digits: Math.max(1, Iem.num(root.a[0], 5))
        textColor: Iem.color(root.a[14], Qure.theme.text, ["#000000"])
    }
    IemLabel { labelArg: root.a[8]; dx: root.a[9]; dy: root.a[10]; size: root.a[12]; colorArg: root.a[15] }
}
