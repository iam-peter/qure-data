// A QML scene rendered by [gfx.qml Scene.qml] – plain Qt Quick, drawn into a
// texture and mixed with the other visuals.
//
//   node.params.level   the sound's level, sent by the patch as "level x"
//   node.output(1, ...) sends a message out of the box's right outlet:
//                       here a click picks the speed and pitch range.
import QtQuick

Item {
    id: scene
    property var node                 // set by Qure Data
    readonly property real level: node && typeof node.params.level === "number" ? node.params.level : 0

    Rectangle {
        anchors.fill: parent
        color: "transparent"
    }

    // Rings that breathe with the level and slowly turn.
    Repeater {
        model: 9
        Rectangle {
            required property int index
            readonly property real base: Math.min(scene.width, scene.height) * (0.12 + index * 0.09)
            anchors.centerIn: parent
            width: base * (1 + scene.level * 0.6 * (1 + index * 0.15))
            height: width
            radius: width / 2
            color: "transparent"
            border.width: 2 + scene.level * 6
            border.color: Qt.hsla((index / 9 + clock.t * 0.03) % 1, 0.8, 0.6, 0.35 + scene.level * 0.6)
            rotation: clock.t * (index % 2 ? 20 : -20)
            Behavior on width { NumberAnimation { duration: 80 } }
        }
    }

    // A dot that follows the last click.
    Rectangle {
        id: dot
        width: 18
        height: 18
        radius: 9
        color: "white"
        opacity: 0.8
        x: scene.width / 2 - 9
        y: scene.height / 2 - 9
        Behavior on x { SmoothedAnimation { velocity: 900 } }
        Behavior on y { SmoothedAnimation { velocity: 900 } }
    }

    FrameAnimation {
        id: clock
        running: true
        property real t: elapsedTime
    }

    MouseArea {
        anchors.fill: parent
        onClicked: (m) => {
            dot.x = m.x - 9
            dot.y = m.y - 9
            if (scene.node)
                scene.node.output(1, [m.x / width, 1 - m.y / height])
        }
    }
}
