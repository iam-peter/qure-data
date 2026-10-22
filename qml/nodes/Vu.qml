// [vu] width height receive label x y font fontsize bg labelcolor scale
// Left inlet: RMS in dB (Pd's vu scale, -100..+12), right inlet: peak.
import QtQuick
import Qure
import "Iem.js" as Iem

Item {
    id: root
    required property NodeHandle node
    readonly property var a: node.args
    property real rms: -100
    property real peak: -100

    function level(db) { return Math.max(0, Math.min(1, (db + 100) / 112)) }

    Connections {
        target: root.node
        function onMessage(selector, args) {
            if (selector === "rms") root.rms = args[0]
            else if (selector === "peak") root.peak = args[0]
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Iem.color(root.a[8], Qure.theme.dark ? "#202225" : "#404040", ["#404040"])
        border.color: Qure.theme.boxBorder
    }
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 1
        x: 2
        width: root.width - 4
        height: (root.height - 2) * root.level(root.rms)
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#ff4040" }
            GradientStop { position: 0.15; color: "#ffd040" }
            GradientStop { position: 0.4; color: "#40d060" }
            GradientStop { position: 1.0; color: "#208040" }
        }
    }
    Rectangle {
        x: 2
        width: root.width - 4
        height: 2
        y: root.height - 2 - (root.height - 4) * root.level(root.peak)
        color: root.peak > 0 ? "#ff4040" : "#ffd040"
        visible: root.peak > -100
    }
}
