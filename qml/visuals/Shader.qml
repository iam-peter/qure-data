// [gfx.shader effect p0 p1 ...]: a built-in effect by name (plasma, scope,
// kaleido, rgbsplit, hue, invert, bars) or a .frag file next to the patch,
// recompiled whenever the file changes. Numbers after the name are the
// starting values of p0, p1...; "p0 0.5" messages change them.
import QtQuick
import Qure

EffectNode {
    id: root
    readonly property var args: node ? node.args : []
    readonly property string name: args.length > 0 ? String(args[0]) : "plasma"

    effect: {
        if (name.endsWith(".frag")) {
            if (name.startsWith("/"))
                return "file://" + name
            return ((node && node.info.dir) || "") + name
        }
        return "qrc:/qt/qml/Qure/visuals/effects/" + name + ".frag"
    }
    defaults: args.slice(1)
}
