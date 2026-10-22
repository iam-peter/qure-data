// The Window menu of every window, as in Pd: the main window (console)
// and one entry per open patch.
import QtQuick
import QtQuick.Controls
import Qure

AppMenu {
    id: menu
    /// The document of the window this menu belongs to (null: main window).
    property PatchDocument current: null

    title: qsTr("&Window")
    IconAction { iconName: "terminal"; text: qsTr("&Main Window"); shortcut: "Ctrl+R"; onTriggered: Qure.showMainWindow() }
    MenuSeparator { visible: Qure.documents.length > 0 }
    Repeater {
        model: Qure.documents
        MenuItem {
            required property PatchDocument modelData
            text: (modelData.modified ? "• " : "") + modelData.title
            font.bold: modelData === menu.current
            icon.source: "icons/file.svg"
            icon.color: palette.windowText
            onTriggered: Qure.openCanvas(modelData.root)
        }
    }
}
