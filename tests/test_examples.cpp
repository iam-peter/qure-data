// The shipped example projects, run headless through the engine: every box
// creates, the DSP graph compiles, and the patches make sound and react to
// the messages their QML nodes send.

#include "Harness.hpp"
#include "PdFile.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>

using namespace qure;
using namespace qure::test;

namespace {

const std::filesystem::path kExamples = QURE_EXAMPLES_DIR;

/// Registers the front-end-only classes a nodes.json declares, as the GUI
/// does (this test has no Qt, so it reads the few fields it needs with a
/// regex).
void registerManifest(Runtime& rt, const std::filesystem::path& manifest) {
    std::ifstream in(manifest);
    REQUIRE(in);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string json = ss.str();
    const std::regex entry(R"re(\{[^{}]*"class"\s*:\s*"([^"]+)"[^{}]*\})re");
    auto intField = [](const std::string& obj, const char* name) {
        std::smatch m;
        const std::regex re(std::string("\"") + name + R"re("\s*:\s*(\d+))re");
        return std::regex_search(obj, m, re) ? std::stoi(m[1]) : -1;
    };
    int count = 0;
    for (auto it = std::sregex_iterator(json.begin(), json.end(), entry); it != std::sregex_iterator(); ++it) {
        const std::string obj = (*it)[0];
        const int ins = intField(obj, "inlets"), outs = intField(obj, "outlets");
        if (ins < 0 && outs < 0)
            continue;
        const bool loadArgs = obj.find("\"loadArgs\": true") != std::string::npos;
        rt.registry().add((*it)[1].str(), [=](const ObjectArgs& a) {
            return makeFrontendObject(a, std::max(0, ins), std::max(0, outs), loadArgs);
        });
        ++count;
    }
    REQUIRE(count > 0);
}

/// The app's built-in QML-only nodes ([kbd], [xypad]) plus the project's own.
void registerNodePack(Runtime& rt, const std::filesystem::path& project) {
    registerManifest(rt, kExamples.parent_path() / "qml" / "nodes" / "nodes.json");
    registerManifest(rt, project / "nodes" / "nodes.json");
}

void requireNothingBroken(const Canvas& c) {
    for (const auto& b : c.boxes()) {
        INFO("box: " << b->displayText() << " in " << c.name());
        CHECK_FALSE(b->broken());
        if (b->child)
            requireNothingBroken(*b->child);
    }
}

Canvas& openProject(Harness& h, const char* name) {
    const auto dir = kExamples / name;
    registerNodePack(h.rt, dir);
    auto r = h.rt.openPatch(dir / "main.pd");
    REQUIRE(r.has_value());
    INFO("compile warnings: " << h.rt.compileWarnings().size());
    CHECK(h.rt.compileWarnings().empty());
    requireNothingBroken(**r);
    h.run(); // install the plan, run loadbangs
    return **r;
}

/// Runs `seconds` of audio; returns the peak of the left channel over the
/// last `windowSeconds`.
float renderPeak(Harness& h, double seconds, double windowSeconds) {
    const auto& cfg = h.rt.engine().config();
    const int frames = cfg.blockSize;
    std::vector<float> out(static_cast<std::size_t>(frames * cfg.outputChannels));
    const int blocks = static_cast<int>(seconds * cfg.sampleRate / frames);
    const int window = static_cast<int>(windowSeconds * cfg.sampleRate / frames);
    float peak = 0;
    for (int b = 0; b < blocks; ++b) {
        h.rt.engine().process(nullptr, out.data(), frames);
        if (b >= blocks - window)
            for (int i = 0; i < frames; ++i)
                peak = std::max(peak, std::abs(out[static_cast<std::size_t>(i * cfg.outputChannels)]));
    }
    h.drain();
    return peak;
}

Box* findClass(Canvas& c, std::string_view cls) { return Harness::find(c, cls); }

void uiEvent(Harness& h, Box* box, std::vector<Atom> args) {
    REQUIRE(box);
    REQUIRE(box->object);
    h.rt.engine().sendUiEvent(box->object->id(), Symbol("out"), args);
}

} // namespace

TEST_CASE("drum machine: the grid's saved pattern reaches the engine and plays", "[examples]") {
    Harness h;
    Canvas& c = openProject(h, "drum-machine");

    // loadArgs: [stepgrid] sent its masks on load, without any GUI.
    CHECK(Symbol("dm-kick").data()->value == 4369);
    CHECK(Symbol("dm-snare").data()->value == 4112);
    CHECK(Symbol("dm-hat").data()->value == 21845);
    CHECK(Symbol("dm-clap").data()->value == 32896);

    CHECK(renderPeak(h, 0.3, 0.3) == 0.0f); // stopped: silence
    h.dsp(true);
    h.send(findClass(c, "tgl"), 0, "float", {Atom(1.0)});
    CHECK(renderPeak(h, 1.0, 1.0) > 0.1f);

    // The grid edits the pattern: all rows off → the beat stops (tails fade).
    uiEvent(h, findClass(c, "stepgrid"), {Atom(0), Atom(0.0), Atom(0.0), Atom(0.0), Atom(0.0)});
    h.run();
    CHECK(Symbol("dm-kick").data()->value == 0);
    CHECK(renderPeak(h, 1.5, 0.3) < 1e-3f);

    // A pad plays its voice on its own.
    uiEvent(h, findClass(c, "pad"), {Atom(0)});
    CHECK(renderPeak(h, 0.1, 0.1) > 0.1f);
}

TEST_CASE("wavetable synth: keyboard notes sound and release", "[examples]") {
    Harness h;
    Canvas& c = openProject(h, "wavetable-synth");
    h.dsp(true);
    Box* kbd = findClass(c, "kbd");
    uiEvent(h, kbd, {Atom(0), Atom(60.0), Atom(100.0)});
    CHECK(renderPeak(h, 0.4, 0.2) > 0.05f);
    // A note-off for another key does not release the held note...
    uiEvent(h, kbd, {Atom(0), Atom(64.0), Atom(0.0)});
    CHECK(renderPeak(h, 0.6, 0.2) > 0.05f);
    // ...its own does (release 400 ms).
    uiEvent(h, kbd, {Atom(0), Atom(60.0), Atom(0.0)});
    CHECK(renderPeak(h, 1.0, 0.1) < 1e-3f);
}

TEST_CASE("wavetable synth: the wave editor's array writes change the sound", "[examples]") {
    Harness h;
    Canvas& c = openProject(h, "wavetable-synth");
    h.dsp(true);
    // What WaveDraw.qml does through Qure.writeArray(): a list to the name.
    std::vector<Atom> silent{Atom(0.0)};
    silent.resize(1 + 35, Atom(0.0));
    h.rt.engine().sendToName(Symbol("wavetable"), sym::list(), silent);
    uiEvent(h, findClass(c, "kbd"), {Atom(0), Atom(60.0), Atom(100.0)});
    CHECK(renderPeak(h, 0.4, 0.2) < 1e-4f); // a flat wavetable is silent
}

TEST_CASE("wavetable synth: the ADSR node's saved arguments configure the envelope", "[examples]") {
    Harness h;
    Canvas& c = openProject(h, "wavetable-synth");
    h.dsp(true);
    // What Adsr.qml sends after a drag: a 2 s attack.
    uiEvent(h, findClass(c, "adsr"), {Atom(0), Atom(2000.0), Atom(200.0), Atom(0.6), Atom(400.0)});
    uiEvent(h, findClass(c, "kbd"), {Atom(0), Atom(60.0), Atom(100.0)});
    const float early = renderPeak(h, 0.1, 0.1);
    const float later = renderPeak(h, 1.5, 0.1);
    CHECK(early < later * 0.3f);
}

TEST_CASE("euclidean ambient: rings drive bells into the delay", "[examples]") {
    Harness h;
    Canvas& c = openProject(h, "euclidean-ambient");
    h.dsp(true);
    CHECK(renderPeak(h, 3.0, 2.0) > 0.01f);

    // All rings at zero hits: once the delay has died away, silence.
    std::vector<Box*> rings;
    for (const auto& b : c.boxes())
        if (b->className().str() == "euclid")
            rings.push_back(b.get());
    REQUIRE(rings.size() == 3);
    for (Box* r : rings)
        uiEvent(h, r, {Atom(0), Atom(16.0), Atom(0.0), Atom(0.0)});
    CHECK(renderPeak(h, 20.0, 0.5) < 1e-3f);
}

TEST_CASE("example patches round-trip through the writer", "[examples]") {
    for (const char* name : {"drum-machine", "wavetable-synth", "euclidean-ambient"}) {
        Harness h;
        const auto dir = kExamples / name;
        registerNodePack(h.rt, dir);
        std::ifstream in(dir / "main.pd");
        std::stringstream ss;
        ss << in.rdbuf();
        auto r = h.rt.openPatch(dir / "main.pd");
        REQUIRE(r.has_value());
        INFO(name);
        CHECK(writePatch(**r) == ss.str());
    }
}
