// [gfx.qml scene.qml]: any QML scene as a visual – Qt Quick, Shapes,
// particles, Qt Quick 3D. The file is resolved next to the patch.
//
// The scene's root item may declare any of these, and they are set:
//   property var node      // node.params: the latest message values;
//                          // node.output(1, [values]) sends from the outlet
//   property Item input0   // the texture inlet, as an Item to sample
import QtQuick
import Qure

Item {
    id: root
    required property NodeHandle node
    property Item input0: null

    anchors.fill: parent
    readonly property string file: node && node.args.length > 0 ? String(node.args[0]) : ""

    Loader {
        id: loader
        anchors.fill: parent
        source: root.file === "" ? "" : root.file.startsWith("/") ? "file://" + root.file : ((root.node && root.node.info.dir) || "") + root.file
        onLoaded: {
            if ("node" in item)
                item.node = root.node
            if ("input0" in item)
                item.input0 = Qt.binding(() => root.input0)
        }
        onStatusChanged: {
            if (status === Loader.Error)
                Qure.postError("gfx.qml: cannot load " + source)
        }
    }
}
