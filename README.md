# Qure Data

A reimplementation of [Pure Data](https://github.com/pure-data/pure-data) in
C++23 with a Qt Quick interface. It opens and saves ordinary `.pd` files.

* **Engine:** lock-free and allocation-free on the audio thread. Patch edits
  are compiled into immutable *plans* and swapped in atomically, so editing
  never blocks or glitches audio. Subpatch inlets and outlets are compiled
  away, and signal buffers are reused through liveness analysis.
* **Canvas:** drawn on the GPU. Boxes use a signed-distance shader and cords
  are screen-space anti-aliased line quads, both batched into a few draw
  calls. Text is drawn only for visible boxes, so even very large patches
  stay smooth.
* **Nodes:** GUI nodes are QML. Sliders, toggles, knobs, a keyboard, a scope
  and an XY pad are each one `.qml` file, and a new node can be QML only,
  with no C++ (see [docs/WRITING_FRONTENDS.md](docs/WRITING_FRONTENDS.md)).

## Build

Requirements: GCC 14 (or another C++23 compiler), CMake ≥ 3.25, Ninja, and
Qt ≥ 6.8 (Quick, QuickControls2, ShaderTools, Test). miniaudio and Catch2
are fetched by CMake. Audio backends (PipeWire, PulseAudio, ALSA, JACK…) are
loaded at runtime, so you don't need their development headers.

```bash
cmake --preset default
```
```bash
cmake --build build/default
```
```bash
./build/default/src/app/qure qml/examples/01-hello-sine.pd
```

The preset expects Qt in `~/Qt/6.12.0/gcc_64`. Change `CMAKE_PREFIX_PATH` in
`CMakePresets.json`, or in a `CMakeUserPresets.json`, if yours is elsewhere.

Useful options: `--no-audio` never opens a sound device; `--dark` /
`--light` choose the theme. Build with `-DQURE_BUILD_APP=OFF` to get only the
engine and its tests.

## Tests

```bash
ctest --test-dir build/default --output-on-failure
```

* `qure_tests` (Catch2) covers the engine: atoms, scheduler, plan swapping
  under a concurrently running engine thread, DSP correctness, and the `.pd`
  reader/writer. It also checks the built-in objects against Pd's
  behaviour.
* `qure_ui_tests` (QtTest) runs the real editor end to end: placing and
  autopatching objects, connecting by dragging, undo/redo, message boxes,
  QML front ends sending through the engine, GPU rendering, and save/load.
  It runs in a private `xvfb-run` display (or Qt's offscreen platform) and
  never opens an audio device.

## Using it

The keys follow Pd: Ctrl+E edit mode, Ctrl+1…5 put object / message / number
/ symbol / comment, Ctrl+Shift+B/T/J/V/K… put bang, toggle, slider, knob…,
Ctrl+D duplicate, Ctrl+/ and Ctrl+. DSP on and off. Drag from an outlet to
connect. Ctrl+wheel zooms; middle-drag or wheel pans. Placing an object
while one box is selected puts the new one below it and connects them, as
Pd's autopatching does.

## Documentation

* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): how it fits together, and
  why.
* [docs/WRITING_OBJECTS.md](docs/WRITING_OBJECTS.md): engine objects in C++.
* [docs/WRITING_FRONTENDS.md](docs/WRITING_FRONTENDS.md): node front ends in
  QML.
