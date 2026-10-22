// [cnv] selectable_size width height send receive label x y font fontsize bg labelcolor
import QtQuick
import Qure
import "Iem.js" as Iem

Item {
    id: root
    required property NodeHandle node
    readonly property var a: node.args
    property var colorOverride

    // "color bg ..." sent to the receive name recolours the canvas.
    Connections {
        target: root.node
        function onMessage(selector, args) {
            if (selector === "color" && args.length > 0)
                root.colorOverride = Iem.color(args[0], undefined, [])
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.colorOverride !== undefined ? root.colorOverride
                                                : Iem.color(root.a[10], Qure.theme.grid, ["#e0e0e0"])
    }
    IemLabel { labelArg: root.a[5]; dx: root.a[6]; dy: root.a[7]; size: root.a[9]; colorArg: root.a[11] }
}
