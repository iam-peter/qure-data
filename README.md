# Qure Data

A reimplementation of [Pure Data](https://github.com/pure-data/pure-data) in
C++23 with a Qt Quick interface. Its patches are `.qd` files in Pd's own
format, so it opens Pd's `.pd` files too, and can save them back as `.pd`.

* **Engine:** lock-free and allocation-free on the audio thread. Patch edits
  are compiled into immutable *plans* and swapped in atomically, so editing
  never blocks or glitches audio. Subpatch inlets and outlets are compiled
  away, and signal buffers are reused through liveness analysis.
* **Canvas:** drawn on the GPU. Boxes use a signed-distance shader and cords
  are screen-space anti-aliased line quads, both batched into a few draw
  calls. Text is drawn only for visible boxes, so even very large patches
  stay smooth.
* **Visuals:** the `gfx.*` objects make real-time graphics like Max's
  Jitter or Pd's GEM: shaders you write as one GLSL function and edit while
  the patch runs, whole QML scenes as layers, feedback, and audio as
  textures (see [docs/VISUALS.md](docs/VISUALS.md)).
* **Nodes:** GUI nodes are QML. Sliders, toggles, knobs, a keyboard, a scope
  and an XY pad are each one `.qml` file, and a new node can be QML only,
  with no C++ (see [docs/WRITING_FRONTENDS.md](docs/WRITING_FRONTENDS.md)).

## Download

Every night with new commits, the
[nightly release](https://github.com/iam-peter/qure-data/releases/tag/nightly)
is rebuilt for Linux (an AppImage and a `.deb`), Windows (a zip) and macOS
(a disk image). The macOS app isn't signed: open it with a right click and
Open the first time.

On Debian and Ubuntu, Qure Data can come from its APT repository instead,
which brings its own Qt:

```bash
sudo curl -fsSLo /usr/share/keyrings/qure-data-archive-keyring.asc https://iam-peter.github.io/qure-data/key.asc
echo "deb [signed-by=/usr/share/keyrings/qure-data-archive-keyring.asc] https://iam-peter.github.io/qure-data/apt stable main" | sudo tee /etc/apt/sources.list.d/qure-data.list
sudo apt update && sudo apt install qure-data
```

Put `nightly` in place of `stable` for the nightly builds. The start page
is at [iam-peter.github.io/qure-data](https://iam-peter.github.io/qure-data/).

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
./build/default/bin/qure
```

The preset expects Qt in `~/Qt/6.12.0/gcc_64`. Change `CMAKE_PREFIX_PATH` in
`CMakePresets.json`, or in a `CMakeUserPresets.json`, if yours is elsewhere.

Useful options: `--no-audio` never opens a sound device; `--dark` /
`--light` choose the theme. Build with `-DQURE_BUILD_APP=OFF` to get only the
engine and its tests.

## Examples

**File ▸ Examples** has three example projects. Each has its own QML nodes
in a `nodes/` folder next to the patch, so you can read them as templates
for your own (see [examples/README.md](examples/README.md)):

* **Drum machine**: a QML step grid stores its pattern in its own
  arguments, and animated QML pads trigger four synthesized voices.
* **Wavetable synth**: draw a waveform with the mouse (written straight
  into an engine array) and shape an ADSR envelope with draggable handles.
* **Euclidean ambient**: euclidean rhythm rings drive FM bells through a
  feedback delay, with a scrolling level plot.
* **Audio visuals**: a small synth drives GPU visuals – its waveform as a
  texture, a QML scene you can click, feedback and the project's own
  shader.

## Tests

```bash
ctest --test-dir build/default --output-on-failure
```

* `qure_tests` (Catch2) covers the engine: atoms, scheduler, plan swapping
  under a concurrently running engine thread, DSP correctness, and the `.pd`
  reader/writer. It also checks the built-in objects against Pd's
  behaviour.
* `qure_tests` also loads each example project, runs its DSP, and checks
  that it plays and reacts to its nodes' messages.
* `qure_ui_example_tests` opens each example project as a user would, from
  the Examples menu, and drives its QML nodes with the mouse.
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

## Releases

GitHub Actions does the releasing, as in
[guckste](https://github.com/iam-peter/guckste):

* `nightly.yml` builds and tests on Linux, Windows and macOS every night
  with new commits (or by hand). It replaces the `nightly` pre-release with
  an AppImage, a `.deb`, a Windows zip and a macOS disk image.
* `promote.yml` (run by hand with a version such as `0.2.0`) turns the
  current nightly into the release `v0.2.0` with the same files.
* `apt.yml` publishes the start page (`pages/`) and a signed APT repository
  with a `stable` and a `nightly` channel on GitHub Pages. It runs after
  both and needs the `APT_SIGNING_KEY` secret (an armored private GPG key).

The packaging scripts are in `packaging/deb/`.

## Documentation

* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): how it fits together, and
  why.
* [docs/WRITING_OBJECTS.md](docs/WRITING_OBJECTS.md): engine objects in C++.
* [docs/WRITING_FRONTENDS.md](docs/WRITING_FRONTENDS.md): node front ends in
  QML.
* [docs/VISUALS.md](docs/VISUALS.md): the visuals module, writing effects
  and scenes.
