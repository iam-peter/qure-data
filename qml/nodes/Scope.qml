// [scope~ samples]: a GPU-drawn oscilloscope fed by the object's snapshot
// buffer (nodes.json sets "snapshots": true so node.samples updates).
import QtQuick
import Qure

Item {
    id: root
    required property NodeHandle node

    Rectangle {
        anchors.fill: parent
        color: Qure.theme.dark ? "#15171a" : "#fbfbf9"
        border.color: Qure.theme.boxBorder
    }
    Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 1; color: Qure.theme.grid }
    WaveView {
        anchors.fill: parent
        anchors.margins: 2
        samples: root.node.samples
        color: Qure.theme.selection
        lineWidth: 1.5
        minimum: -1
        maximum: 1
    }
}
