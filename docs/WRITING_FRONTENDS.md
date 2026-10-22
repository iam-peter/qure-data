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
* `loadArgs: true` (front-end-only classes) makes the engine object send
  its creation arguments out of outlet 0 when the patch loads. Together
  with `node.setArg()` this gives a node persistent state that reaches the
  patch even if the node is never drawn: front ends exist only while
  visible.
* `aliases` lists alternative class names.

Node packs are found in three places, and none needs recompiling:

* `qml/nodes/nodes.json`: the built-in nodes, compiled into the app.
* `~/.local/share/QureData/nodes/<pack>/nodes.json`: your own, loaded at
  startup.
* `nodes/nodes.json` **next to a patch**: a project's own nodes, loaded
  before the patch opens. The [example projects](../examples/README.md)
  work this way.

Abstractions (`.pd` files) are found next to the patch, or in
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
dark theme), `Qure.dsp` and more. For front ends that work on shared
data:

| Member | Meaning |
|---|---|
| `Qure.readArray(name)` | contents of an array (`[table]`, `[array define]`, graph array) |
| `Qure.writeArray(name, offset, values)` | write values into it, through the engine |
| `Qure.sendMessage("name sel args")` | send to a receive name, like `; name sel args` |

## Patterns that work well

The example projects show each of these in a few dozen lines of QML:

* **State in the arguments, timing in the engine**
  ([StepGrid.qml](../examples/drum-machine/nodes/StepGrid.qml),
  [Euclid.qml](../examples/euclidean-ambient/nodes/Euclid.qml),
  [Adsr.qml](../examples/wavetable-synth/nodes/Adsr.qml)). The node
  edits parameters and sends them; the patch does the sample-accurate
  work. GUI latency never affects the music.
* **Editing engine data directly**
  ([WaveDraw.qml](../examples/wavetable-synth/nodes/WaveDraw.qml)): the
  array is the single source of truth, and the node reads it back.
* **Animation from engine events**
  ([TriggerPad.qml](../examples/drum-machine/nodes/TriggerPad.qml)):
  `node.received` starts a QML animation.
* **Display-only nodes**
  ([Plotter.qml](../examples/euclidean-ambient/nodes/Plotter.qml)): keep a
  little history in QML and draw it with `WaveView`.

## Things to keep in mind

* In edit mode, front ends get no mouse input, because the canvas handles
  selection and dragging.
* Front ends exist only while visible and zoomed in to at least 40 %.
  Below that, the box is drawn as a plain rectangle. Keep state in the
  engine object, or in `setArg`, not only in QML.
* Interaction should go through `node.send` / `node.output`. The engine
  answers with a notification that updates `node.value`, so the display
  always shows what the engine has.
