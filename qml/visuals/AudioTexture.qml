// [gfx.audio~ samples]: the latest samples of a signal as a texture,
// one pixel high. Red: the waveform (0.5 is silence). Green: the level.
import QtQuick
import Qure

Item {
    id: root
    required property NodeHandle node
    anchors.fill: parent

    SampleTexture {
        anchors.fill: parent
        samples: root.node ? root.node.samples : []
    }
}
