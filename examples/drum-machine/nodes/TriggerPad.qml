// [pad name]: a drum pad. Click to trigger; lights up whenever its inlet
// gets a message (here: every time the voice plays, from the sequencer).
//
// What this node shows: pure QML animation driven by engine events
// (node.received), and output with node.output(0) – a bang.
import QtQuick
import Qure

Item {
    id: root
    required property NodeHandle node

    readonly property string label: node.args.length > 0 ? String(node.args[0]) : ""
    readonly property var tints: ({ kick: "#ff6b4a", snare: "#ffb02e", hat: "#3ec7a8", clap: "#5c8dff" })
    readonly property color tint: tints[label] || Qure.theme.selection
    property real glow: 0

    function flash() { glowAnim.restart() }

    NumberAnimation {
        id: glowAnim
        target: root
        property: "glow"
        from: 1
        to: 0
        duration: 320
        easing.type: Easing.OutCubic
    }

    Connections {
        target: root.node
        function onReceived() { root.flash() }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: 8
        color: Qt.tint(Qure.theme.dark ? "#25282d" : "#e9ebee", Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.25 + 0.75 * root.glow))
        border.color: root.tint
        border.width: 1.5
        scale: 1 - 0.06 * root.glow

        Text {
            anchors.centerIn: parent
            text: root.label
            font.pixelSize: Math.max(8, root.height / 5)
            font.bold: true
            color: root.glow > 0.4 ? "white" : Qure.theme.text
        }
    }

    MouseArea {
        anchors.fill: parent
        onPressed: {
            root.node.output(0) // a bang
            root.flash()
        }
    }
}
