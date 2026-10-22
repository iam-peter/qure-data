# Writing node front ends (QML)

A front end is what a box looks like and how you interact with it. Every
GUI node shipped with Qure Data (toggle, sliders, keyboard, scope…) is one
QML file in `qml/nodes/`, registered in `qml/nodes/nodes.json`.

## A front end in 20 lines

```qml
// MyButton.qml
import QtQuick
import Qure

Item {
    required property NodeHandle node      // always provided

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: node.value ? Qure.theme.selection : Qure.theme.boxFill
        border.color: Qure.theme.boxBorder
    }
    MouseArea {
        anchors.fill: parent
        onPressed: node.output(0, [1])
        onReleased: node.output(0, [0])
    }
}
```

The root item is sized to the box, in canvas units. The canvas scales it
when zooming, so draw at 1:1.

## Registering it

Add an entry to a `nodes.json`:

```json
{ "class": "mybutton", "qml": "MyButton.qml",
  "inlets": 1, "outlets": 1, "size": [40, 40],
  "help": "momentary button" }
```

* With `inlets` / `outlets`, the class needs **no C++ at all**. The engine
  creates a generic object that forwards every incoming message to QML
  (`node.received(inlet, selector, args)`), and sends whatever QML passes
  to `node.output(outlet, value)`. `Keyboard.qml` and `XYPad.qml` work this
  way.
* Without them, the entry gives a look to an existing engine object, for
  example `tgl` or `scope~`.
* `size` is the default size. `widthArg` / `heightArg` (plus
  `widthScale` / `heightScale`) take it from a creation argument instead.
* `snapshots: true` makes `node.samples` follow the object's bulk data
  (scopes, arrays). Draw it with `WaveView { samples: node.samples }`, which
  runs on the GPU.
* `aliases` lists alternative class names.

User node packs are loaded from
`~/.local/share/QureData/nodes/<pack>/nodes.json` at startup, so you can
add nodes without recompiling. Abstractions (`.pd` files) go in
`~/.local/share/QureData/abstractions/`.

## The `node` API

| Member | Meaning |
|---|---|
| `node.value` | last value the engine reported: number, string or list |
| `node.args` | creation arguments (after the class name) |
| `node.className` | the box's class |
| `node.editMode`, `node.selected` | canvas state, e.g. for hints |
| `node.samples` | bulk data (with `snapshots`) |
| `node.info` | extras for special boxes, e.g. array graph ranges |
| `node.send(v)` | interact: a number / string / list goes to the object |
| `node.sendMessage(sel, args)` | send any message to the object |
| `node.output(i, v)` | front-end-only classes: send `v` out of outlet `i` |
| `node.click()` | a bang |
| `node.setArg(i, v)` | persist state into the box's arguments (saved with the patch) |
| `message(sel, args)` | signal: every notification from the engine |
| `received(inlet, sel, args)` | signal: front-end-only classes' input |
| `bang()` | signal: the object flashed (e.g. `bng`) |

The `Qure` singleton exposes `Qure.theme` (colours that follow the light or
dark theme), `Qure.dsp` and more.

## Things to keep in mind

* In edit mode, front ends get no mouse input, because the canvas handles
  selection and dragging.
* Front ends exist only while visible and zoomed in to at least 40 %.
  Below that, the box is drawn as a plain rectangle. Keep state in the
  engine object, or in `setArg`, not only in QML.
* Interaction should go through `node.send` / `node.output`. The engine
  answers with a notification that updates `node.value`, so the display
  always shows what the engine has.
