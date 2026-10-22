// A visual node running one effect (see EffectCompiler): two texture
// inputs, eight parameters, time and resolution. Shader.qml and Mix.qml are
// thin wrappers around it.
import QtQuick
import Qure

Item {
    id: root
    required property NodeHandle node
    property Item input0: null
    property Item input1: null
    /// The effect's source: a .frag file (local or qrc) with `vec4 effect(vec2 uv)`.
    property url effect
    /// Default parameter values, e.g. from the creation arguments.
    property var defaults: []

    anchors.fill: parent

    // "p0 0.5" messages win over the defaults.
    function param(i) {
        const v = node ? node.params["p" + i] : undefined
        if (typeof v === "number")
            return v
        return typeof defaults[i] === "number" ? defaults[i] : 0
    }

    EffectCompiler {
        id: compiler
        source: root.effect
    }

    ShaderEffectSource {
        id: src0
        sourceItem: root.input0
        hideSource: true
        live: true
        visible: false
    }
    ShaderEffectSource {
        id: src1
        sourceItem: root.input1
        hideSource: true
        live: true
        visible: false
    }

    FrameAnimation {
        id: frames
        running: true
    }

    ShaderEffect {
        anchors.fill: parent
        visible: compiler.shader.toString() !== ""
        fragmentShader: compiler.shader
        property var source0: src0
        property var source1: src1
        property real time: frames.elapsedTime
        property size resolution: Qt.size(width, height)
        property real p0: root.param(0)
        property real p1: root.param(1)
        property real p2: root.param(2)
        property real p3: root.param(3)
        property real p4: root.param(4)
        property real p5: root.param(5)
        property real p6: root.param(6)
        property real p7: root.param(7)
    }
}
