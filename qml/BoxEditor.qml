// In-place text editor for object, message and comment boxes, with class
// name completion for objects.
import QtQuick
import QtQuick.Controls
import Qure

Item {
    id: root
    required property CanvasView view
    property string kind: "object"
    property bool committing: false
    visible: false

    function show(rect, text, k) {
        kind = k
        x = rect.x
        y = rect.y
        width = Math.max(rect.width, 80 * view.zoom)
        input.text = text
        visible = true
        input.forceActiveFocus()
        input.selectAll()
        completions.update()
    }

    function commit() {
        if (!visible || committing)
            return
        committing = true
        visible = false
        view.commitEdit(input.text)
        committing = false
    }

    function cancel() {
        visible = false
        view.cancelEdit()
    }

    Rectangle {
        anchors.fill: input
        anchors.margins: -2
        color: Qure.theme.boxFill
        border.color: Qure.theme.selection
        border.width: 1.5
        radius: 2
    }

    TextEdit {
        id: input
        width: Math.max(root.width, contentWidth + 8)
        color: Qure.theme.text
        selectionColor: Qure.theme.selection
        selectedTextColor: "white"
        font.family: "JetBrains Mono"
        font.pixelSize: Math.max(6, 12 * root.view.zoom)
        leftPadding: 4 * root.view.zoom
        topPadding: 3 * root.view.zoom
        bottomPadding: 3 * root.view.zoom
        wrapMode: TextEdit.NoWrap
        persistentSelection: false

        onTextChanged: completions.update()
        onActiveFocusChanged: if (!activeFocus && root.visible) root.commit()

        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Escape) {
                root.cancel()
                event.accepted = true
            } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                       && !(event.modifiers & Qt.ShiftModifier)) {
                if (completions.visible && completions.currentIndex >= 0 && completions.accepting)
                    completions.accept()
                root.commit()
                event.accepted = true
            } else if (event.key === Qt.Key_Tab && completions.visible) {
                completions.accept()
                event.accepted = true
            } else if (event.key === Qt.Key_Down && completions.visible) {
                completions.currentIndex = Math.min(completions.count - 1, completions.currentIndex + 1)
                completions.accepting = true
                event.accepted = true
            } else if (event.key === Qt.Key_Up && completions.visible) {
                completions.currentIndex = Math.max(0, completions.currentIndex - 1)
                completions.accepting = true
                event.accepted = true
            }
        }
    }

    // Completion list for the class name (first word of an object box).
    ListView {
        id: completions
        property bool accepting: false
        y: input.height + 4
        width: 220
        height: Math.min(count, 8) * 22
        visible: root.kind === "object" && count > 0
        clip: true
        model: []
        currentIndex: 0
        highlightMoveDuration: 0

        function update() {
            accepting = false
            const t = input.text
            if (root.kind !== "object" || t.length === 0 || t.indexOf(" ") >= 0) {
                model = []
                return
            }
            const all = Qure.objectClasses
            const starts = all.filter(c => c.startsWith(t))
            model = starts.length === 1 && starts[0] === t ? [] : starts.slice(0, 50)
            currentIndex = 0
        }
        function accept() {
            if (currentIndex < 0 || currentIndex >= count)
                return
            input.text = model[currentIndex] + " "
            input.cursorPosition = input.text.length
        }

        Rectangle {
            z: -1
            anchors.fill: parent
            color: Qure.theme.boxFill
            border.color: Qure.theme.boxBorder
        }
        highlight: Rectangle { color: Qure.theme.selection; opacity: 0.25 }
        delegate: Item {
            required property string modelData
            required property int index
            width: ListView.view.width
            height: 22
            Row {
                anchors.verticalCenter: parent.verticalCenter
                leftPadding: 6
                spacing: 8
                Text {
                    text: modelData
                    font.family: "JetBrains Mono"
                    font.pixelSize: 12
                    color: Qure.theme.text
                }
                Text {
                    text: Qure.helpFor(modelData)
                    font.pixelSize: 11
                    color: Qure.theme.commentText
                    elide: Text.ElideRight
                    width: 120
                }
            }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    completions.currentIndex = index
                    completions.accept()
                    input.forceActiveFocus()
                }
            }
        }
    }
}
