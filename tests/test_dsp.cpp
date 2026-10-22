#include "Harness.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace qure;
using namespace qure::test;
using Catch::Matchers::WithinAbs;

namespace {

/// Runs `blocks` blocks through Engine::process and returns the left
/// output channel of the last one.
std::vector<float> render(Harness& h, int blocks = 3) {
    const auto& cfg = h.rt.engine().config();
    std::vector<float> out(static_cast<std::size_t>(cfg.blockSize * cfg.outputChannels));
    std::vector<float> left(static_cast<std::size_t>(cfg.blockSize));
    for (int b = 0; b < blocks; ++b)
        h.rt.engine().process(nullptr, out.data(), cfg.blockSize);
    for (int i = 0; i < cfg.blockSize; ++i)
        left[static_cast<std::size_t>(i)] = out[static_cast<std::size_t>(i * cfg.outputChannels)];
    h.drain();
    return left;
}

} // namespace

TEST_CASE("constant signal reaches the dac", "[dsp]") {
    Harness h;
    h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 sig~ 0.25;
#X obj 10 40 *~ 2;
#X obj 10 70 dac~;
#X connect 0 0 1 0;
#X connect 1 0 2 0;
)");
    h.dsp(true);
    const auto out = render(h);
    for (float s : out)
        CHECK_THAT(s, WithinAbs(0.5, 1e-7));
}

TEST_CASE("fan-in is summed and unconnected inlets read their scalar", "[dsp]") {
    Harness h;
    h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 sig~ 1;
#X obj 60 10 sig~ 2;
#X obj 10 40 +~;
#X obj 10 70 dac~;
#X obj 100 40 +~ 10;
#X connect 0 0 2 0;
#X connect 1 0 2 0;
#X connect 2 0 3 0;
#X connect 2 0 4 0;
#X connect 4 0 3 0;
)");
    h.dsp(true);
    const auto out = render(h);
    // (1 + 2) + right inlet scalar 0 = 3, plus (3 + 10) = 16 at the dac.
    CHECK_THAT(out[0], WithinAbs(16.0, 1e-6));
}

TEST_CASE("signals flow through subpatch inlet~/outlet~", "[dsp]") {
    Harness h;
    h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 sig~ 3;
#N canvas 0 0 300 200 inner 0;
#X obj 10 10 inlet~;
#X obj 10 40 *~ 2;
#X obj 10 70 outlet~;
#X connect 0 0 1 0;
#X connect 1 0 2 0;
#X restore 10 40 pd inner;
#X obj 10 70 dac~;
#X connect 0 0 1 0;
#X connect 1 0 2 0;
)");
    h.dsp(true);
    CHECK_THAT(render(h)[10], WithinAbs(6.0, 1e-6));
}

TEST_CASE("osc~ produces a cosine at the right frequency", "[dsp]") {
    Harness h;
    h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 osc~ 750;
#X obj 10 40 dac~;
#X connect 0 0 1 0;
)");
    h.dsp(true);
    // 750 Hz at 48 kHz = 64 samples per period: one block is one period.
    const auto out = render(h, 4);
    CHECK_THAT(out[0], WithinAbs(1.0, 1e-3));
    CHECK_THAT(out[16], WithinAbs(0.0, 1e-3));
    CHECK_THAT(out[32], WithinAbs(-1.0, 1e-3));
}

TEST_CASE("send~/receive~ are ordered without delay", "[dsp]") {
    Harness h;
    h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 receive~ x;
#X obj 10 40 dac~;
#X obj 100 10 sig~ 0.5;
#X obj 100 40 send~ x;
#X connect 0 0 1 0;
#X connect 2 0 3 0;
)");
    h.dsp(true);
    // receive~ was created first, but the compiler schedules send~ before
    // it, so the very first DSP block already carries the value.
    h.rt.engine().tick(); // install + dsp on
    const auto out = render(h, 2);
    CHECK_THAT(out[0], WithinAbs(0.5, 1e-7));
}

TEST_CASE("DSP loops are reported and skipped", "[dsp]") {
    Harness h;
    h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 +~;
#X obj 10 40 *~;
#X connect 0 0 1 0;
#X connect 1 0 0 0;
)");
    REQUIRE(h.rt.compileWarnings().size() == 1);
    CHECK(h.rt.compileWarnings()[0].find("DSP loop") != std::string::npos);
}

TEST_CASE("buffers are reused across a long chain", "[dsp]") {
    Harness h;
    auto& c = h.rt.newPatch();
    Box* prev = &c.addBox(BoxKind::Object, 0, 0, parseAtoms("sig~ 1"));
    for (int i = 0; i < 1000; ++i) {
        Box& next = c.addBox(BoxKind::Object, 0, i, parseAtoms("*~ 1"));
        c.connect(*prev, 0, next, 0);
        prev = &next;
    }
    Box& dac = c.addBox(BoxKind::Object, 0, 2000, parseAtoms("dac~"));
    c.connect(*prev, 0, dac, 0);
    h.rt.commit();
    const Plan* plan = h.rt.engine().latestPlan();
    REQUIRE(plan);
    // A linear chain needs only a handful of buffers, not one per object.
    CHECK(plan->bufferCount < 8);
    h.dsp(true);
    CHECK_THAT(render(h)[5], WithinAbs(1.0, 1e-6));
}
