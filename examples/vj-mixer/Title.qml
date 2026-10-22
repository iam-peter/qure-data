// Channel B of the VJ mixer: a Qt Quick scene rendered by [gfx.qml].
// Every bang into the box (the beat) makes the title jump and spawns a ring;
// "text ..." messages change the words.
import QtQuick

Item {
    id: scene
    property var node                       // set by Qure Data
    readonly property string words: node && typeof node.params.text === "string" ? node.params.text : "QURE DATA"
    property real kick: 0
    property int beats: 0

    Connections {
        target: scene.node
        // node is set after the scene is created; don't warn until then.
        ignoreUnknownSignals: true
        function onReceived(inlet, selector, args) {
            if (selector !== "bang")
                return
            kickAnim.restart()
            rings.itemAt(scene.beats % rings.count).fire()
            scene.beats++
        }
    }
    NumberAnimation { id: kickAnim; target: scene; property: "kick"; from: 1; to: 0; duration: 450; easing.type: Easing.OutCubic }

    // Rings that expand from the centre on each beat – a pool of six, reused.
    Repeater {
        id: rings
        model: 6
        Rectangle {
            id: ring
            required property int index
            anchors.centerIn: parent
            width: 0
            height: width
            radius: width / 2
            opacity: 0
            color: "transparent"
            border.width: 6
            border.color: Qt.hsla((index * 0.17) % 1, 0.9, 0.6, 1)
            function fire() { grow.restart() }
            ParallelAnimation {
                id: grow
                NumberAnimation { target: ring; property: "width"; from: 20; to: scene.width * 1.4; duration: 1800; easing.type: Easing.OutQuad }
                NumberAnimation { target: ring; property: "opacity"; from: 1; to: 0; duration: 1800 }
            }
        }
    }

    Text {
        anchors.centerIn: parent
        text: scene.words
        color: "white"
        font.pixelSize: scene.height * (0.18 + 0.06 * scene.kick)
        font.bold: true
        font.letterSpacing: scene.height * 0.02
        rotation: Math.sin(clock.elapsedTime * 0.7) * 6
        style: Text.Outline
        styleColor: Qt.hsla((clock.elapsedTime * 0.05) % 1, 0.9, 0.55, 1)
    }

    FrameAnimation { id: clock; running: true }
}
