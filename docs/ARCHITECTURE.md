# Architecture

```
┌──────────────────────── GUI thread ────────────────────────┐   ┌──── engine thread ────┐
│ QML windows ── CanvasView ── PatchDocument (undo) ──┐      │   │                       │
│      │            │ GPU layers                      │      │   │  Engine::process()    │
│ node front ends   │ (SDF boxes, cords, text)        ▼      │   │   install plan        │
│ (QML, per class)  │                         Runtime/Canvas │   │   drain commands      │
│      │            │                        (document model)│   │   fire clocks         │
│      └─ NodeHandle ◄── Session pump ◄─ events ring ◄───────┼───┤   run DSP program     │
│                                  ─► commands ring ─────────┼──►│                       │
│                    compilePlan() ─► atomic plan swap ──────┼──►│  (audio callback or   │
└────────────────────────────────────────────────────────────┘   │   timer thread)       │
                                                                  └───────────────────────┘
```

## Layers

| Directory | What | Depends on |
|---|---|---|
| `src/core` | Engine, objects, document model, `.pd` I/O, compiler | C++23 standard library only |
| `src/audio` | Drivers: miniaudio device, silent timer | core, miniaudio |
| `src/app` | Qt Quick GUI (library `qure_gui`, QML module `Qure`) + `main.cpp` | core, audio, Qt |
| `qml/` | Windows, editor, node front ends, examples | the `Qure` module |
| `tests/` | Catch2 engine tests, QtTest UI tests | |

The core has no Qt dependency. It could run headless, as a plugin, or in
another GUI.

## The engine: no locks on the audio thread

Pd protects everything with one global lock, which the GUI takes while
editing. Here the two threads share nothing mutable:

* **Plans.** Each edit recompiles the open patches into an immutable `Plan`
  on the GUI thread: flat routing tables, a topologically sorted DSP
  program, and pre-assigned signal buffers. The plan is published with one
  atomic exchange, and the engine installs it at the next block boundary.
  Objects keep their state across plans (oscillator phase, delay lines), so
  editing does not click.
* **Lifetime.** Plans hold `shared_ptr`s to their objects, so an object
  removed from the patch stays alive until no plan the engine might still
  run refers to it. It is then destroyed on the GUI thread. Activation
  (binding receive names) and deactivation (unsetting clocks) happen on the
  engine thread when a plan is installed, and are idempotent so a skipped
  plan cannot leak work.
* **Messages** cross in either direction through wait-free SPSC byte rings
  (`SpscRing`). A command records which plan generation it needs, so a
  message to a just-created object waits until that object is live.
* **Bulk data** (scopes, arrays) goes through seqlocked `SnapshotBuffer`s
  that the GUI polls.
* **Symbols** carry their own binding lists, like Pd's `s_thing`, so
  `[send]` and `[receive]` resolve in O(1) without hashing or allocation.

### Compiler

`compilePlan()` turns the document tree into a plan:

1. Control connections are resolved *through* subpatch `[inlet]`/`[outlet]`
   boxes, so a message reaches its final receiver with no forwarding hops.
2. Signal connections are resolved through `[inlet~]`/`[outlet~]` into one
   flat graph, which is sorted topologically. `[send~]`→`[receive~]` and
   `[throw~]`→`[catch~]` get *implicit ordering edges* where these don't
   create cycles, which removes the one-block delay Pd has there. A DSP
   loop is reported and only the looping objects are skipped.
3. Signal buffers are allocated by liveness. A chain of 1000 objects uses a
   handful of buffers instead of 1000, which keeps the working set in
   cache. Fan-in is summed explicitly. An unconnected signal inlet reads its
   scalar, as in Pd.

### Timing

64-sample blocks, as in Pd. Clocks live in logical time (ms, double
precision) in an intrusive sorted list. They are set, unset and fired
without allocating, and are drift free (`[metro]` re-arms relative to its
due time). Control floats are double precision.

## The GUI: GPU for scale, QML for convenience

QML alone would not handle 100k boxes: every `Item` costs memory and
instantiation time. A custom scene graph alone would make GUI nodes painful
to write. So the canvas is split:

* **`CanvasLayer` (boxes)**: every box background in a few batched
  geometry nodes, chunked at 4096 boxes, with a signed-distance shader
  (`box.frag`) that draws Pd's shapes (message flag, atom corner, dashed
  broken box) crisply at any zoom. Text uses `QSGTextNode`, only for
  visible boxes and only above 35 % zoom.
* **QML front ends**: real components for GUI classes, *instantiated only
  while visible* and above 40 % zoom (at most 600 at a time). Otherwise the
  box layer draws them as plain boxes. This keeps panning cheap and keeps
  very large patches viewable.
* **`CanvasLayer` (overlay)**: all cords as screen-space anti-aliased quads
  (`cord.vert` widens them in the vertex shader, so width is exact at every
  zoom and any number of cords is one draw call per 8192). Iolets are drawn
  around the viewport only. Selection outlines, rubber band and the cord
  being dragged are redrawn each frame.

Pan and zoom change only a transform matrix. Geometry is rebuilt only
when the model or selection changes.

`PatchDocument` owns undo (`QUndoStack`). Commands address canvases by box
id path and recreate deleted boxes with their **original ids** and
positions, so later commands still apply after undo/redo. Copy/paste uses
Pd's own clipboard text format.

## Compatibility

Reading and writing round-trips Pd files, including `#X obj/msg/text/
floatatom/symbolatom/listbox`, subpatches, graphs with arrays (`#X array`,
`#A`), `#X coords`, `, f N` widths and broken objects (connections are
kept). Unknown records are preserved. Abstractions get their own `$0` and
arguments. There are about 175 built-in classes covering the common Pd
vanilla vocabulary, including `expr`, `vline~`, filters, delays and tables.

## Choices

* **C++23, GCC 14.** Uses `std::expected`, `std::format`, `std::span`,
  `std::jthread`, ranges and concepts. C++20 modules are not used yet:
  CMake/GCC support works, but Qt's moc and tooling (qmlls, clangd) don't
  handle them well yet.
* **miniaudio** for I/O. It is a single file that loads backends at
  runtime, so builds need no ALSA/JACK/Pulse headers. It is a C library,
  confined to one translation unit behind `AudioDriver`.
* **Qt 6.8+**: `QSGTextNode`, `qt_add_shaders`, declarative QML module
  registration.

## Roadmap

* MIDI I/O (`notein`, `ctlin`…), e.g. via RtMidi, and an audio settings
  dialog.
* Graph-on-parent rendering of GUI objects inside abstractions.
* Data structures (`struct`, `pointer`), `clone`, `readsf~`/`writesf~`,
  `fft~`.
* Editing arrays with the mouse; array resize updating the saved
  `#X array` size.
* Incremental geometry updates for very large patches during drags (rebuild
  only dirty chunks), and incremental plan compilation.
* Parallel DSP: independent sub-graphs of the plan on worker threads.
* Help patches, an object browser, and a property editor per GUI class.
