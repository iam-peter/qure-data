// The label every iemgui can carry, positioned like Pd does.
import QtQuick
import Qure
import "Iem.js" as Iem

Text {
    property var labelArg
    property var dx
    property var dy
    property var size
    property var colorArg

    text: Iem.label(labelArg)
    visible: text.length > 0
    x: Iem.num(dx, 0)
    y: Iem.num(dy, 0) - height / 2
    font.pixelSize: Math.max(6, Iem.num(size, 10))
    font.family: "DejaVu Sans Mono"
    color: Iem.color(colorArg, Qure.theme.text, ["#000000"])
}
