// [bng] size hold interrupt init send receive label x y font fontsize bg fg labelcolor
import QtQuick
import Qure
import "Iem.js" as Iem

Item {
    id: root
    required property NodeHandle node
    readonly property var a: node.args
    property bool lit: false

    Rectangle {
        anchors.fill: parent
        color: Iem.bg(root.a[11], Qure.theme)
        border.color: Qure.theme.boxBorder
        border.width: 1
    }
    Rectangle {
        anchors.fill: parent
        anchors.margins: Math.max(1.5, parent.width * 0.08)
        radius: width / 2
        color: root.lit ? Iem.color(root.a[12], Qure.theme.selection, ["#000000"]) : "transparent"
        border.color: Qure.theme.boxBorder
        border.width: 1
    }
    IemLabel { labelArg: root.a[6]; dx: root.a[7]; dy: root.a[8]; size: root.a[10]; colorArg: root.a[13] }

    Timer {
        id: hold
        interval: Math.max(10, Iem.num(root.a[1], 250))
        onTriggered: root.lit = false
    }
    Connections {
        target: root.node
        function onBang() { root.lit = true; hold.restart() }
    }
    MouseArea {
        anchors.fill: parent
        onPressed: root.node.click()
    }
}
