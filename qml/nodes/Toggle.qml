// [tgl] size init send receive label x y font fontsize bg fg labelcolor init_value nonzero
import QtQuick
import Qure
import "Iem.js" as Iem

Item {
    id: root
    required property NodeHandle node
    readonly property var a: node.args
    readonly property bool on: Number(node.value) !== 0 && node.value !== undefined
    readonly property color mark: Iem.color(a[10], Qure.theme.selection, ["#000000"])

    // Remember the state in the patch when "init" is set (Pd's behaviour).
    onOnChanged: if (Iem.num(a[1], 0) !== 0) node.setArg(12, Number(node.value) || 0)

    Rectangle {
        anchors.fill: parent
        color: Iem.bg(root.a[9], Qure.theme)
        border.color: Qure.theme.boxBorder
        border.width: 1
    }
    // The cross.
    Repeater {
        model: 2
        Rectangle {
            required property int index
            visible: root.on
            anchors.centerIn: parent
            width: root.width * 0.92
            height: Math.max(1.5, root.width / 9)
            radius: height / 2
            rotation: index === 0 ? 45 : -45
            color: root.mark
        }
    }
    IemLabel { labelArg: root.a[4]; dx: root.a[5]; dy: root.a[6]; size: root.a[8]; colorArg: root.a[11] }

    MouseArea {
        anchors.fill: parent
        onPressed: root.node.click()
    }
}
