// The engine side of the visuals module: texture iolets, how they may be
// connected, and what gfx objects pass to the GUI. (Rendering is tested in
// tests/ui/test_visuals.cpp.)

#include "Harness.hpp"
#include "PdFile.hpp"
#include "Snapshot.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <fstream>
#include <sstream>

using namespace qure;
using namespace qure::test;

TEST_CASE("gfx objects have texture iolets", "[visuals]") {
    Harness h;
    auto& c = h.rt.newPatch();
    auto& shader = c.addBox(BoxKind::Object, 0, 0, parseAtoms("gfx.shader plasma"));
    auto& mix = c.addBox(BoxKind::Object, 0, 0, parseAtoms("gfx.mix"));
    auto& qml = c.addBox(BoxKind::Object, 0, 0, parseAtoms("gfx.qml scene.qml"));
    auto& audio = c.addBox(BoxKind::Object, 0, 0, parseAtoms("gfx.audio~"));
    REQUIRE_FALSE(shader.broken());
    CHECK(shader.inlets == std::vector{IoletKind::Texture, IoletKind::Texture});
    CHECK(shader.outlets == std::vector{IoletKind::Texture});
    CHECK(mix.inlets == std::vector{IoletKind::Texture, IoletKind::Texture, IoletKind::Control});
    CHECK(qml.outlets == std::vector{IoletKind::Texture, IoletKind::Control});
    CHECK(audio.inlets == std::vector{IoletKind::Signal});
    CHECK(audio.outlets == std::vector{IoletKind::Texture});
}

TEST_CASE("texture cords only join texture iolets", "[visuals]") {
    Harness h;
    auto& c = h.rt.newPatch();
    auto& shader = c.addBox(BoxKind::Object, 0, 0, parseAtoms("gfx.shader plasma"));
    auto& window = c.addBox(BoxKind::Object, 0, 40, parseAtoms("gfx.window"));
    auto& print = c.addBox(BoxKind::Object, 0, 80, parseAtoms("print"));
    auto& osc = c.addBox(BoxKind::Object, 0, 80, parseAtoms("osc~ 1"));
    auto& msg = c.addBox(BoxKind::Message, 0, 80, parseAtoms("p0 0.5"));
    auto& audio = c.addBox(BoxKind::Object, 0, 80, parseAtoms("gfx.audio~"));

    CHECK(c.connect(shader, 0, window, 0));       // texture → texture
    CHECK_FALSE(c.connect(shader, 0, print, 0));  // texture → control
    CHECK_FALSE(c.connect(osc, 0, window, 0));    // signal → texture
    CHECK_FALSE(c.connect(audio, 0, osc, 0));     // texture → signal
    CHECK(c.connect(msg, 0, shader, 0));          // control → texture: messages
    CHECK(c.connect(osc, 0, audio, 0));           // signal → gfx.audio~
    CHECK(c.connections()[0].isTexture());
}

TEST_CASE("texture cords carry no messages; control messages reach the GUI", "[visuals]") {
    Harness h;
    auto& c = h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 gfx.qml scene.qml;
#X obj 10 50 gfx.shader invert;
#X msg 120 10 p0 0.25;
#X obj 120 50 print fromscene;
#X connect 0 0 1 0;
#X connect 2 0 1 0;
#X connect 0 1 3 0;
)");
    // The compiler gives texture outlets no message targets.
    const Plan* plan = h.rt.engine().latestPlan();
    REQUIRE(plan);
    const Object* qml = c.boxes()[0]->object.get();
    const auto it = std::ranges::find_if(plan->bindings, [&](const auto& b) { return b.object == qml; });
    REQUIRE(it != plan->bindings.end());
    CHECK(plan->outletTables[it->firstOutlet].count == 0);     // texture outlet
    CHECK(plan->outletTables[it->firstOutlet + 1].count == 1); // control outlet → print

    // A message into the shader's texture inlet goes to the GUI as
    // "in <inlet> <selector> <args>".
    h.rt.engine().sendUiEvent(c.boxes()[2]->object->id(), Symbol("click"), {});
    h.run();
    REQUIRE_FALSE(h.notificationArgs.empty());
    const auto& n = h.notificationArgs.back();
    CHECK(h.notifications.back().selector.str() == "in");
    REQUIRE(n.size() == 3);
    CHECK(n[0].asFloat() == 0);
    CHECK(n[1].asSymbol().str() == "p0");
    CHECK(n[2].asFloat() == 0.25);

    // A QML scene's node.output(1, ...) leaves through the control outlet.
    h.rt.engine().sendUiEvent(qml->id(), Symbol("out"), std::vector<Atom>{Atom(1.0), Atom(0.5), Atom(0.75)});
    CHECK(h.take() == std::vector<std::string>{"fromscene: 0.5 0.75"});
}

TEST_CASE("gfx.audio~ publishes the latest samples", "[visuals]") {
    Harness h;
    auto& c = h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 sig~ 0.5;
#X obj 10 50 gfx.audio~ 128;
#X connect 0 0 1 0;
)");
    h.dsp(true);
    h.run(4);
    const SnapshotBuffer* snap = c.boxes()[1]->object->snapshot();
    REQUIRE(snap);
    std::vector<float> samples;
    std::uint64_t seen = ~std::uint64_t{0};
    REQUIRE(snap->read(samples, seen));
    REQUIRE(samples.size() == 128);
    for (float s : samples)
        CHECK_THAT(s, Catch::Matchers::WithinAbs(0.5, 1e-6));
}

TEST_CASE("the audio-visuals example loads and plays", "[visuals][examples]") {
    Harness h;
    auto r = h.rt.openPatch(std::filesystem::path(QURE_EXAMPLES_DIR) / "audio-visuals" / "main.qd");
    REQUIRE(r);
    for (const auto& b : (*r)->boxes()) {
        INFO(b->displayText());
        CHECK_FALSE(b->broken());
    }
    CHECK(h.rt.compileWarnings().empty());
    h.run();
    h.dsp(true);
    const auto& cfg = h.rt.engine().config();
    std::vector<float> out(static_cast<std::size_t>(cfg.blockSize * cfg.outputChannels));
    float peak = 0;
    for (int b = 0; b < 750; ++b) { // one second
        h.rt.engine().process(nullptr, out.data(), cfg.blockSize);
        for (float s : out)
            peak = std::max(peak, std::abs(s));
    }
    CHECK(peak > 0.02f);
}

TEST_CASE("the graphics examples load and round-trip through the writer", "[visuals][examples]") {
    for (const char* name : {"audio-visuals", "vj-mixer"}) {
        INFO(name);
        Harness h;
        const auto file = std::filesystem::path(QURE_EXAMPLES_DIR) / name / "main.qd";
        std::ifstream in(file);
        std::stringstream ss;
        ss << in.rdbuf();
        auto r = h.rt.openPatch(file);
        REQUIRE(r);
        for (const auto& b : (*r)->boxes()) {
            INFO(b->displayText());
            CHECK_FALSE(b->broken());
        }
        CHECK(h.rt.compileWarnings().empty());
        CHECK(writePatch(**r) == ss.str());
    }
}
