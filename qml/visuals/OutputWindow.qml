// [gfx.window width height]: shows what arrives at its texture inlet.
// Messages: "fullscreen 1/0", "title text...".
import QtQuick
import QtQuick.Window
import Qure

Window {
    id: win
    required property NodeHandle node
    /// The node item feeding the window (set by the visual engine).
    property Item content: null
    /// Where the visual engine puts the node items.
    readonly property Item stage: stage

    readonly property var args: node ? node.args : []
    readonly property var params: node ? node.params : ({})
    width: typeof args[0] === "number" ? args[0] : 640
    height: typeof args[1] === "number" ? args[1] : 360
    color: "black"
    title: typeof params.title === "string" ? params.title : qsTr("Qure Data – visuals")
    visibility: params.fullscreen ? Window.FullScreen : Window.Windowed

    Item {
        id: stage
        anchors.fill: parent
    }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: win.content
        hideSource: true
        live: true
        visible: win.content !== null
    }
    // Escape leaves fullscreen.
    Shortcut {
        sequence: "Escape"
        onActivated: win.visibility = Window.Windowed
    }
}
