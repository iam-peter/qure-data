// [gfx.mix amount]: crossfades its two inputs; the right inlet (or an
// "amount" message) sets 0..1.
import QtQuick
import Qure

EffectNode {
    id: root
    effect: "qrc:/qt/qml/Qure/visuals/effects/mix.frag"
    defaults: {
        const p = node ? node.params : {}
        const a = node ? node.args : []
        const amount = typeof p.in2 === "number" ? p.in2
                     : typeof p.amount === "number" ? p.amount
                     : typeof a[0] === "number" ? a[0] : 0.5
        return [amount]
    }
}
