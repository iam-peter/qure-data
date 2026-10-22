// The main window: console, DSP switch, audio status – Pd's "Pd window".
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Qure

ApplicationWindow {
    id: main
    width: 720
    height: 460
    visible: true
    title: "Qure Data"
    color: Qure.theme.background

    readonly property var patchWindows: new Map()
    property bool quitting: false

    // Closing the main window quits, as in Pd: patches without a console
    // and a menu to stop DSP would play on with no way to bring it back.
    onClosing: (close) => {
        if (!quitting) {
            close.accepted = false
            quit()
        }
    }

    /// Quits, first asking about unsaved patches.
    function quit() {
        const unsaved = Qure.documents.filter(d => d.modified)
        if (unsaved.length > 0) {
            quitQuestion.unsaved = unsaved
            quitQuestion.open()
            return
        }
        quitNow()
    }

    function quitNow() {
        quitting = true
        for (const w of Array.from(patchWindows.values())) {
            w.forceClose = true
            w.close()
        }
        Qt.quit()
    }

    MessageDialog {
        id: quitQuestion
        objectName: "quitQuestion"
        property var unsaved: []
        text: unsaved.length === 1 ? qsTr("Save changes to %1 before quitting?").arg(unsaved[0].title)
                                   : qsTr("Save changes to %n patch(es) before quitting?", "", unsaved.length)
        informativeText: unsaved.length > 1 ? unsaved.map(d => d.title).join(", ") : ""
        buttons: (unsaved.length === 1 ? MessageDialog.Save : MessageDialog.SaveAll) | MessageDialog.Discard | MessageDialog.Cancel
        onButtonClicked: (button) => {
            if (button === MessageDialog.Discard) {
                main.quitNow()
            } else if (button === MessageDialog.Save || button === MessageDialog.SaveAll) {
                // A patch that was never saved needs a file name: show it
                // and let its own Save dialog take over.
                for (const d of unsaved) {
                    if (d.fileUrl.toString() === "" || !d.save()) {
                        main.openCanvas(d.root)
                        Qure.postError(qsTr("not quitting: %1 is not saved").arg(d.title))
                        return
                    }
                }
                main.quitNow()
            }
        }
    }

    function openCanvas(ref) {
        if (!ref || !ref.valid)
            return
        const existing = patchWindows.get(ref)
        if (existing) {
            existing.raise()
            existing.requestActivate()
            return
        }
        const w = patchWindowComponent.createObject(null, { canvasRef: ref })
        patchWindows.set(ref, w)
        w.closing.connect(() => { patchWindows.delete(ref); w.destroy() })
        w.show()
    }

    function openExample(path) {
        const doc = Qure.openExample(path)
        if (doc)
            openCanvas(doc.root)
    }

    function newPatch() {
        const doc = Qure.newPatch()
        if (doc)
            openCanvas(doc.root)
    }

    Connections {
        target: Qure
        function onCanvasOpenRequested(ref) { main.openCanvas(ref) }
        function onMainWindowRequested() {
            main.show()
            main.raise()
            main.requestActivate()
        }
        function onQuitRequested() { main.quit() }
    }

    Component {
        id: patchWindowComponent
        PatchWindow {}
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open patch")
        nameFilters: [qsTr("Patches (*.qd *.pd)"), qsTr("Qure Data patches (*.qd)"), qsTr("Pd patches (*.pd)"), qsTr("All files (*)")]
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            for (const url of selectedFiles) {
                const doc = Qure.openPatch(url)
                if (doc)
                    main.openCanvas(doc.root)
            }
        }
    }

    menuBar: MenuBar {
        AppMenu {
            title: qsTr("&File")
            IconAction { iconName: "file-plus"; text: qsTr("&New Patch"); shortcut: StandardKey.New; onTriggered: main.newPatch() }
            IconAction { iconName: "folder-open"; text: qsTr("&Open…"); shortcut: StandardKey.Open; onTriggered: openDialog.open() }
            AppMenu {
                title: qsTr("&Examples")
                iconName: "book-open"
                // Projects: a folder with main.qd, its own QML nodes and abstractions.
                Repeater {
                    model: Qure.examples.filter(e => e.group === "projects")
                    MenuItem {
                        required property var modelData
                        text: modelData.title
                        icon.source: "icons/folder.svg"
                        icon.color: palette.windowText
                        onTriggered: main.openExample(modelData.path)
                    }
                }
                MenuSeparator {}
                AppMenu {
                    title: qsTr("&Basics")
                    iconName: "graduation-cap"
                    Repeater {
                        model: Qure.examples.filter(e => e.group === "basics")
                        MenuItem {
                            required property var modelData
                            text: modelData.title
                            icon.source: "icons/file.svg"
                            icon.color: palette.windowText
                            onTriggered: main.openExample(modelData.path)
                        }
                    }
                }
            }
            MenuSeparator {}
            IconAction { iconName: "log-out"; text: qsTr("&Quit"); shortcut: StandardKey.Quit; onTriggered: main.quit() }
        }
        AppMenu {
            title: qsTr("&Media")
            IconAction { iconName: "power"; text: qsTr("DSP &On"); shortcut: "Ctrl+/"; onTriggered: Qure.dsp = true }
            IconAction { iconName: "power-off"; text: qsTr("DSP O&ff"); shortcut: "Ctrl+."; onTriggered: Qure.dsp = false }
            MenuSeparator {}
            AppMenu {
                id: outputMenu
                title: qsTr("Audio &Output")
                iconName: "audio-lines"
                Repeater {
                    model: Qure.playbackDevices
                    MenuItem {
                        required property string modelData
                        required property int index
                        text: modelData
                        onTriggered: Qure.restartAudio(index, -1)
                    }
                }
            }
            IconAction { iconName: "speaker"; text: qsTr("Default Audio Device"); onTriggered: Qure.restartAudio(-1, -1) }
            IconAction { iconName: "refresh-cw"; text: qsTr("Rescan Devices"); onTriggered: Qure.refreshDevices() }
        }
        AppMenu {
            title: qsTr("&View")
            IconAction { iconName: "moon"; text: qsTr("&Dark Theme"); checkable: true; checked: Qure.darkTheme; onTriggered: Qure.darkTheme = checked }
            IconAction { iconName: "eraser"; text: qsTr("&Clear Console"); shortcut: "Ctrl+Shift+L"; onTriggered: Qure.console.clear() }
        }
        WindowMenu {}
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 12
            Switch {
                text: qsTr("DSP")
                checked: Qure.dsp
                onToggled: Qure.dsp = checked
            }
            Label {
                text: qsTr("load %1%").arg(Math.round(Qure.cpuLoad * 100))
                color: Qure.cpuLoad > 0.8 ? "#d03a3a" : palette.text
                visible: Qure.dsp
            }
            Label {
                Layout.fillWidth: true
                text: Qure.audioStatus
                elide: Text.ElideMiddle
                opacity: 0.7
            }
            ToolButton { action: IconAction { iconName: "file-plus"; text: qsTr("New"); onTriggered: main.newPatch() } }
            ToolButton { action: IconAction { iconName: "folder-open"; text: qsTr("Open"); onTriggered: openDialog.open() } }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ListView {
            id: log
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Qure.console
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            onCountChanged: Qt.callLater(positionViewAtEnd)
            delegate: Text {
                required property var model
                readonly property int level: model.level
                width: ListView.view.width
                leftPadding: 8
                rightPadding: 8
                text: model.text
                wrapMode: Text.Wrap
                font.family: "JetBrains Mono"
                font.pixelSize: 12
                textFormat: Text.PlainText
                color: level === 0 ? "#d03a3a" : level === 1 ? "#c88a12" : Qure.theme.text
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 6
            Label { text: ";" ; font.family: "JetBrains Mono" }
            TextField {
                id: messageField
                Layout.fillWidth: true
                font.family: "JetBrains Mono"
                placeholderText: qsTr("send a message, e.g.  pd dsp 1")
                onAccepted: {
                    Qure.sendMessage(text)
                    text = ""
                }
            }
        }
    }

    Component.onCompleted: {
        // Windows for patches opened from the command line.
        for (const doc of Qure.documents)
            openCanvas(doc.root)
    }
}
