// [hradio] / [vradio]: size new_old init number send receive label x y font fontsize bg fg labelcolor value
import QtQuick
import Qure
import "Iem.js" as Iem

Item {
    id: root
    required property NodeHandle node
    readonly property var a: node.args
    readonly property bool vertical: node.className === "vradio" || node.className === "vdl"
    readonly property int cells: Math.max(1, Iem.num(a[3], 8))
    readonly property int current: typeof node.value === "number" ? node.value : Iem.num(a[14], 0)
    readonly property real cell: vertical ? height / cells : width / cells

    Repeater {
        model: root.cells
        Rectangle {
            required property int index
            x: root.vertical ? 0 : index * root.cell
            y: root.vertical ? index * root.cell : 0
            width: root.vertical ? root.width : root.cell + 1
            height: root.vertical ? root.cell + 1 : root.height
            color: Iem.bg(root.a[11], Qure.theme)
            border.color: Qure.theme.boxBorder
            border.width: 1
            Rectangle {
                anchors.fill: parent
                anchors.margins: Math.max(2, root.cell * 0.22)
                visible: index === root.current
                color: Iem.color(root.a[12], Qure.theme.selection, ["#000000"])
            }
        }
    }
    IemLabel { labelArg: root.a[6]; dx: root.a[7]; dy: root.a[8]; size: root.a[10]; colorArg: root.a[13] }

    MouseArea {
        anchors.fill: parent
        onPressed: (m) => root.node.send(Math.floor((root.vertical ? m.y : m.x) / root.cell))
    }
}
