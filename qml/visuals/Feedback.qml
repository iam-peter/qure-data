// [gfx.feedback decay zoom rotate]: video feedback. Each frame shows the
// input on top of the previous frame, faded by `decay`, scaled by `zoom`
// and turned by `rotate` (radians per frame). Messages: decay, zoom, rotate.
import QtQuick
import Qure

Item {
    id: root
    required property NodeHandle node
    property Item input0: null

    anchors.fill: parent

    function param(name, index, fallback) {
        if (!node)
            return fallback
        const v = node.params[name]
        if (typeof v === "number")
            return v
        return typeof node.args[index] === "number" ? node.args[index] : fallback
    }

    EffectCompiler {
        id: compiler
        source: "qrc:/qt/qml/Qure/visuals/effects/feedback.frag"
    }
    ShaderEffectSource {
        id: input
        sourceItem: root.input0
        hideSource: true
        live: true
        visible: false
    }
    // The previous frame of `fx` itself: a recursive texture.
    ShaderEffectSource {
        id: previous
        sourceItem: fx
        recursive: true
        live: true
        visible: false
    }
    ShaderEffect {
        id: fx
        anchors.fill: parent
        visible: compiler.shader.toString() !== ""
        fragmentShader: compiler.shader
        property var source0: input
        property var source1: previous
        property real time: 0
        property size resolution: Qt.size(width, height)
        property real p0: root.param("decay", 0, 0.92)
        property real p1: root.param("zoom", 1, 1.01)
        property real p2: root.param("rotate", 2, 0.003)
        property real p3: 0
        property real p4: 0
        property real p5: 0
        property real p6: 0
        property real p7: 0
    }
}
