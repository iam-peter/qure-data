// A window showing one canvas (a patch or a subpatch).
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Qure

ApplicationWindow {
    id: win
    required property CanvasRef canvasRef
    readonly property PatchDocument doc: canvasRef ? canvasRef.document : null
    property bool forceClose: false

    width: 900
    height: 640
    color: Qure.theme.background
    title: (doc && doc.modified ? "• " : "") + (canvasRef ? canvasRef.title : "") + (view.editMode ? qsTr("  [edit]") : "")

    // A deleted subpatch or a closed document takes its windows with it.
    Connections {
        target: win.canvasRef
        function onValidChanged() { if (!win.canvasRef.valid) { win.forceClose = true; win.close() } }
    }

    onClosing: (close) => {
        if (forceClose || !canvasRef || !canvasRef.isRoot)
            return
        if (doc && doc.modified) {
            close.accepted = false
            saveQuestion.open()
            return
        }
        Qure.closeDocument(doc)
    }

    function save() {
        if (!doc)
            return
        if (doc.fileUrl.toString() === "")
            saveDialog.open()
        else
            doc.save()
    }

    MessageDialog {
        id: saveQuestion
        text: qsTr("Save changes to %1?").arg(win.doc ? win.doc.title : "")
        buttons: MessageDialog.Save | MessageDialog.Discard | MessageDialog.Cancel
        onButtonClicked: (button) => {
            if (button === MessageDialog.Save) {
                win.save()
                if (win.doc.modified)
                    return
            } else if (button !== MessageDialog.Discard) {
                return
            }
            const d = win.doc
            win.forceClose = true
            win.close()
            Qure.closeDocument(d)
        }
    }

    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pd"
        nameFilters: [qsTr("Pd patches (*.pd)")]
        onAccepted: win.doc.saveAs(selectedFile)
    }

    // ---- menus ----------------------------------------------------------
    menuBar: MenuBar {
        Menu {
            title: qsTr("&File")
            Action { text: qsTr("&Save"); shortcut: StandardKey.Save; onTriggered: win.save() }
            Action { text: qsTr("Save &As…"); shortcut: StandardKey.SaveAs; onTriggered: saveDialog.open() }
            MenuSeparator {}
            Action { text: qsTr("&Close"); shortcut: StandardKey.Close; onTriggered: win.close() }
        }
        Menu {
            title: qsTr("&Edit")
            Action {
                text: qsTr("&Undo %1").arg(win.doc ? win.doc.undoText : "")
                enabled: win.doc && win.doc.canUndo
                shortcut: StandardKey.Undo
                onTriggered: win.doc.undo()
            }
            Action {
                text: qsTr("&Redo %1").arg(win.doc ? win.doc.redoText : "")
                enabled: win.doc && win.doc.canRedo
                shortcut: StandardKey.Redo
                onTriggered: win.doc.redo()
            }
            MenuSeparator {}
            Action { text: qsTr("Cu&t"); shortcut: StandardKey.Cut; onTriggered: view.cutSelection() }
            Action { text: qsTr("&Copy"); shortcut: StandardKey.Copy; onTriggered: view.copySelection() }
            Action { text: qsTr("&Paste"); shortcut: StandardKey.Paste; onTriggered: view.paste() }
            Action { text: qsTr("&Duplicate"); shortcut: "Ctrl+D"; onTriggered: view.duplicateSelection() }
            Action { text: qsTr("Select &All"); shortcut: StandardKey.SelectAll; onTriggered: view.selectAll() }
            MenuSeparator {}
            Action {
                text: qsTr("&Edit Mode")
                checkable: true
                checked: view.editMode
                shortcut: "Ctrl+E"
                onTriggered: view.editMode = !view.editMode
            }
            Action { text: qsTr("&Open Subpatch"); shortcut: "Ctrl+Return"; onTriggered: view.openSelected() }
        }
        Menu {
            title: qsTr("&Put")
            Action { text: qsTr("&Object"); shortcut: "Ctrl+1"; onTriggered: view.placeBox("object") }
            Action { text: qsTr("&Message"); shortcut: "Ctrl+2"; onTriggered: view.placeBox("message") }
            Action { text: qsTr("&Number"); shortcut: "Ctrl+3"; onTriggered: view.placeBox("floatatom") }
            Action { text: qsTr("&Symbol"); shortcut: "Ctrl+4"; onTriggered: view.placeBox("symbolatom") }
            Action { text: qsTr("&List"); shortcut: "Ctrl+9"; onTriggered: view.placeBox("listbox") }
            Action { text: qsTr("&Comment"); shortcut: "Ctrl+5"; onTriggered: view.placeBox("comment", "comment") }
            MenuSeparator {}
            Action { text: qsTr("&Bang"); shortcut: "Ctrl+Shift+B"; onTriggered: view.placeBox("object", "bng 19 250 50 0 empty empty empty 0 -10 0 12 #fcfcfc #000000 #000000", true) }
            Action { text: qsTr("&Toggle"); shortcut: "Ctrl+Shift+T"; onTriggered: view.placeBox("object", "tgl 19 0 empty empty empty 17 7 0 10 #fcfcfc #000000 #000000 0 1", true) }
            Action { text: qsTr("Number &2"); shortcut: "Ctrl+Shift+N"; onTriggered: view.placeBox("object", "nbx 5 17 -1e+37 1e+37 0 0 empty empty empty 0 -10 0 12 #fcfcfc #000000 #000000 0 256", true) }
            Action { text: qsTr("&Hslider"); shortcut: "Ctrl+Shift+J"; onTriggered: view.placeBox("object", "hsl 128 17 0 127 0 0 empty empty empty -2 -10 0 12 #fcfcfc #000000 #000000 0 1", true) }
            Action { text: qsTr("&Vslider"); shortcut: "Ctrl+Shift+V"; onTriggered: view.placeBox("object", "vsl 17 128 0 127 0 0 empty empty empty 0 -10 0 12 #fcfcfc #000000 #000000 0 1", true) }
            Action { text: qsTr("&Knob"); shortcut: "Ctrl+Shift+K"; onTriggered: view.placeBox("object", "knob 50 0 127 0 empty empty", true) }
            Action { text: qsTr("Hra&dio"); shortcut: "Ctrl+Shift+D"; onTriggered: view.placeBox("object", "hradio 18 1 0 8 empty empty empty 0 -10 0 12 #fcfcfc #000000 #000000 0", true) }
            Action { text: qsTr("Vrad&io"); shortcut: "Ctrl+Shift+I"; onTriggered: view.placeBox("object", "vradio 18 1 0 8 empty empty empty 0 -10 0 12 #fcfcfc #000000 #000000 0", true) }
            Action { text: qsTr("V&U Meter"); shortcut: "Ctrl+Shift+U"; onTriggered: view.placeBox("object", "vu 18 160 empty empty -1 -10 0 12 #404040 #000000 1 0", true) }
            Action { text: qsTr("C&anvas"); shortcut: "Ctrl+Shift+C"; onTriggered: view.placeBox("object", "cnv 15 100 60 empty empty empty 20 12 0 14 #e0e0e0 #404040 0", true) }
            MenuSeparator {}
            Action { text: qsTr("Piano &Keyboard"); onTriggered: view.placeBox("object", "kbd 3 48", true) }
            Action { text: qsTr("&XY Pad"); onTriggered: view.placeBox("object", "xypad 120", true) }
            Action { text: qsTr("Sco&pe~"); onTriggered: view.placeBox("object", "scope~ 512", true) }
        }
        Menu {
            title: qsTr("&View")
            Action { text: qsTr("Zoom &In"); shortcut: StandardKey.ZoomIn; onTriggered: view.zoomBy(1.25) }
            Action { text: qsTr("Zoom &Out"); shortcut: StandardKey.ZoomOut; onTriggered: view.zoomBy(0.8) }
            Action { text: qsTr("&Actual Size"); shortcut: "Ctrl+0"; onTriggered: view.resetZoom() }
            Action { text: qsTr("&Fit Patch"); shortcut: "Ctrl+Shift+F"; onTriggered: view.fitToView() }
        }
        Menu {
            title: qsTr("&Media")
            Action { text: qsTr("DSP &On"); shortcut: "Ctrl+/"; onTriggered: Qure.dsp = true }
            Action { text: qsTr("DSP O&ff"); shortcut: "Ctrl+."; onTriggered: Qure.dsp = false }
        }
    }

    // ---- canvas ---------------------------------------------------------
    Rectangle {
        anchors.fill: parent
        color: Qure.theme.background

        CanvasView {
            id: view
            anchors.fill: parent
            canvas: win.canvasRef
            focus: true
            Component.onCompleted: forceActiveFocus()

            onEditRequested: (rect, text, kind) => editor.show(rect, text, kind)
            onEditClosed: editor.visible = false
            onContextMenuRequested: (pos, onBox) => {
                contextMenu.onBox = onBox
                contextMenu.popup(view, pos)
            }

            BoxEditor {
                id: editor
                view: view
                z: 10
            }
        }

        // Pinch / trackpad zoom.
        PinchHandler {
            target: null
            property real startZoom: 1
            onActiveChanged: if (active) startZoom = view.zoom
            onActiveScaleChanged: view.zoom = startZoom * activeScale
        }
    }

    Menu {
        id: contextMenu
        property bool onBox: false
        MenuItem { text: qsTr("Open"); enabled: contextMenu.onBox; onTriggered: view.openSelected() }
        MenuSeparator {}
        MenuItem { text: qsTr("Cut"); enabled: contextMenu.onBox; onTriggered: view.cutSelection() }
        MenuItem { text: qsTr("Copy"); enabled: contextMenu.onBox; onTriggered: view.copySelection() }
        MenuItem { text: qsTr("Paste"); onTriggered: view.paste() }
        MenuItem { text: qsTr("Duplicate"); enabled: contextMenu.onBox; onTriggered: view.duplicateSelection() }
        MenuItem { text: qsTr("Delete"); enabled: contextMenu.onBox; onTriggered: view.deleteSelection() }
    }

    footer: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 14
            Label {
                text: view.editMode ? qsTr("EDIT") : qsTr("RUN")
                font.bold: true
                color: view.editMode ? Qure.theme.selection : palette.text
            }
            Label {
                Layout.fillWidth: true
                text: view.hoverText
                elide: Text.ElideRight
                opacity: 0.8
            }
            Label { text: qsTr("%1 boxes").arg(view.boxCount); opacity: 0.6 }
            Label { text: qsTr("%1 widgets").arg(view.liveWidgets); opacity: 0.6 }
            Label { text: Math.round(view.zoom * 100) + "%"; opacity: 0.6 }
            Label { text: Qure.dsp ? qsTr("DSP on") : qsTr("DSP off"); color: Qure.dsp ? "#2a9d4b" : palette.text }
        }
    }
}
