#include "Harness.hpp"
#include "PdFile.hpp"
#include "Snapshot.hpp"
#include "Table.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <string>

using namespace qure;
using namespace qure::test;
using Catch::Matchers::WithinAbs;

namespace {

/// Computes `blocks` blocks and returns the given output channel of all of
/// them, concatenated. Engine::process hands out the previously computed
/// block, so the first call only flushes what was computed before (and
/// starts the first block after any queued messages).
std::vector<float> renderAll(Harness& h, int blocks, std::size_t channel = 0) {
    const auto& cfg = h.rt.engine().config();
    const auto bs = static_cast<std::size_t>(cfg.blockSize);
    const auto channels = static_cast<std::size_t>(cfg.outputChannels);
    std::vector<float> out(bs * channels);
    std::vector<float> result;
    for (int b = 0; b <= blocks; ++b) {
        h.rt.engine().process(nullptr, out.data(), cfg.blockSize);
        if (b > 0)
            for (std::size_t i = 0; i < bs; ++i)
                result.push_back(out[i * channels + channel]);
    }
    h.drain();
    return result;
}

/// Turns DSP on without running a block, so the next renderAll() starts
/// with the very first DSP block.
void startDsp(Harness& h) { h.rt.engine().setDsp(true); }

/// Index of the largest absolute sample.
std::size_t peakIndex(const std::vector<float>& v) {
    return static_cast<std::size_t>(std::ranges::max_element(v, {}, [](float x) { return std::fabs(x); }) - v.begin());
}

/// A graph with array `name` holding `values` (saved with the patch).
std::string graphArray(std::string_view name, const std::vector<float>& values) {
    std::string s = "#N canvas 0 0 450 300 (subpatch) 0;\n";
    s += std::format("#X array {} {} float 3;\n#A 0", name, values.size());
    for (float v : values)
        s += ' ' + formatFloat(v);
    s += ";\n#X coords 0 1 4 -1 200 140 1 0 0;\n#X restore 10 10 graph;\n";
    return s;
}

const SnapshotBuffer* snapshotOf(Canvas& c, std::string_view cls) {
    Box* b = Harness::find(c, cls);
    REQUIRE(b);
    REQUIRE(b->object);
    return b->object->snapshot();
}

} // namespace

// ---------------------------------------------------------------------------
// Filters

TEST_CASE("lop~ converges to a DC input", "[filter]") {
    Harness h;
    h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 sig~ 1;
#X obj 10 40 lop~ 1000;
#X obj 10 70 dac~;
#X connect 0 0 1 0;
#X connect 1 0 2 0;
)");
    startDsp(h);
    const auto out = renderAll(h, 20);
    CHECK(out[0] > 0.1f); // coef = 2π·1000/48000 ≈ 0.13
    CHECK(out[0] < 0.2f);
    CHECK(out[10] > out[5]);
    CHECK_THAT(out.back(), WithinAbs(1.0, 1e-4));
}

TEST_CASE("hip~ removes DC and passes a step through at first", "[filter]") {
    Harness h;
    h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 sig~ 1;
#X obj 10 40 hip~ 100;
#X obj 10 70 dac~;
#X connect 0 0 1 0;
#X connect 1 0 2 0;
)");
    startDsp(h);
    const auto out = renderAll(h, 60);
    CHECK(out[0] > 0.9f);
    CHECK_THAT(out.back(), WithinAbs(0.0, 1e-4));
}

TEST_CASE("bp~ passes its center frequency and attenuates far away", "[filter]") {
    Harness h;
    h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 osc~ 1500;
#X obj 10 40 bp~ 1500 5;
#X obj 10 70 dac~;
#X obj 100 10 osc~ 12000;
#X obj 100 40 bp~ 1500 5;
#X connect 0 0 1 0;
#X connect 1 0 2 0;
#X connect 3 0 4 0;
#X connect 4 0 2 1;
)");
    h.dsp(true);
    renderAll(h, 100);
    const auto center = renderAll(h, 4, 0);
    const auto far = renderAll(h, 4, 1);
    const float peak = std::fabs(center[peakIndex(center)]);
    CHECK(peak > 0.8f);
    CHECK(peak < 1.2f);
    CHECK(std::fabs(far[peakIndex(far)]) < 0.05f);
}

TEST_CASE("biquad~ applies coefficients and rejects unstable ones", "[filter]") {
    Harness h;
    auto& c = h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 sig~ 1;
#X obj 10 40 biquad~ 0 0 0.5 0.25 0;
#X obj 10 70 dac~;
#X connect 0 0 1 0;
#X connect 1 0 2 0;
)");
    startDsp(h);
    auto out = renderAll(h, 2);
    CHECK_THAT(out[0], WithinAbs(0.5, 1e-6));  // ff1·x[0]
    CHECK_THAT(out[1], WithinAbs(0.75, 1e-6)); // ff1·x[1] + ff2·x[0]
    // fb1 = 3 is unstable: everything is set to zero, like Pd.
    h.send(Harness::find(c, "biquad~"), 0, "list", {Atom(3.0), Atom(0.0), Atom(1.0), Atom(0.0), Atom(0.0)});
    out = renderAll(h, 2);
    CHECK_THAT(out.back(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("rpole~ and rzero~ have the expected impulse responses", "[filter]") {
    Harness h;
    auto& c = h.open("#N canvas 0 0 400 300 12;\n" + graphArray("dirac", {1, 0, 0, 0}) + R"(#X obj 10 200 tabplay~ dirac;
#X obj 10 230 rpole~ 0.5;
#X obj 10 260 rzero~ 0.5;
#X obj 10 290 dac~;
#X connect 1 0 2 0;
#X connect 2 0 3 0;
#X connect 3 0 4 0;
#X obj 100 230 rzero~ 0.5;
#X obj 100 260 dac~ 2;
#X connect 1 0 5 0;
#X connect 5 0 6 0;
)");
    h.dsp(true);
    h.send(Harness::find(c, "tabplay~"), 0, "bang");
    // Left: rzero~ undoes rpole~ exactly.
    const auto left = renderAll(h, 1);
    CHECK_THAT(left[0], WithinAbs(1.0, 1e-6));
    CHECK_THAT(left[1], WithinAbs(0.0, 1e-6));
    CHECK_THAT(left[2], WithinAbs(0.0, 1e-6));
    // Right: 1 - 0.5 z^-1.
    h.send(Harness::find(c, "tabplay~"), 0, "bang");
    const auto right = renderAll(h, 1, 1);
    CHECK_THAT(right[0], WithinAbs(1.0, 1e-6));
    CHECK_THAT(right[1], WithinAbs(-0.5, 1e-6));
    CHECK_THAT(right[2], WithinAbs(0.0, 1e-6));
}

TEST_CASE("cpole~ with a real coefficient behaves like rpole~", "[filter]") {
    Harness h;
    auto& c = h.open("#N canvas 0 0 400 300 12;\n" + graphArray("dirac", {1, 0, 0, 0}) + R"(#X obj 10 200 tabplay~ dirac;
#X obj 10 230 cpole~ 0.5 0;
#X obj 10 260 dac~;
#X connect 1 0 2 0;
#X connect 2 0 3 0;
)");
    h.dsp(true);
    h.send(Harness::find(c, "tabplay~"), 0, "bang");
    const auto out = renderAll(h, 1);
    CHECK_THAT(out[0], WithinAbs(1.0, 1e-6));
    CHECK_THAT(out[1], WithinAbs(0.5, 1e-6));
    CHECK_THAT(out[2], WithinAbs(0.25, 1e-6));
}

TEST_CASE("vcf~ resonates at its center frequency", "[filter]") {
    Harness h;
    h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 osc~ 3000;
#X obj 100 10 sig~ 3000;
#X obj 10 40 vcf~ 10;
#X obj 10 70 dac~;
#X connect 0 0 2 0;
#X connect 1 0 2 1;
#X connect 2 0 3 0;
)");
    h.dsp(true);
    const auto out = renderAll(h, 100);
    const std::vector<float> last(out.end() - 256, out.end());
    const float peak = std::fabs(last[peakIndex(last)]);
    CHECK(peak > 0.7f);
    CHECK(peak < 1.3f);
}

TEST_CASE("samphold~ samples when the control signal drops", "[filter]") {
    Harness h;
    h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 sig~ 3;
#X obj 100 10 phasor~ 1000;
#X obj 10 40 samphold~;
#X obj 10 70 dac~;
#X connect 0 0 2 0;
#X connect 1 0 2 1;
#X connect 2 0 3 0;
)");
    startDsp(h);
    const auto out = renderAll(h, 2);
    CHECK_THAT(out[0], WithinAbs(0.0, 1e-9));
    CHECK_THAT(out.back(), WithinAbs(3.0, 1e-9)); // the phasor wrapped at sample 48
}

TEST_CASE("threshold~ bangs on crossings", "[filter]") {
    Harness h;
    auto& c = h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 sig~ 0;
#X obj 10 40 threshold~ 0.5 10 0.2 10;
#X obj 10 70 print hi;
#X obj 100 70 print lo;
#X connect 0 0 1 0;
#X connect 1 0 2 0;
#X connect 1 1 3 0;
)");
    h.dsp(true);
    renderAll(h, 2);
    h.log.clear();
    h.send(Harness::find(c, "sig~"), 0, "float", {Atom(1.0)});
    renderAll(h, 2);
    CHECK(h.log == std::vector<std::string>{"hi: bang"});
    h.log.clear();
    h.send(Harness::find(c, "sig~"), 0, "float", {Atom(0.0)});
    renderAll(h, 2); // still in the 10 ms dead time
    CHECK(h.log.empty());
    renderAll(h, 10);
    CHECK(h.log == std::vector<std::string>{"lo: bang"});
}

TEST_CASE("vline~ ramps and jumps sample-accurately", "[filter]") {
    Harness h;
    auto& c = h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 40 vline~;
#X obj 10 70 dac~;
#X connect 0 0 1 0;
)");
    h.dsp(true);
    Box* vline = Harness::find(c, "vline~");
    // Ramp to 1 over 10 ms (480 samples).
    h.send(vline, 1, "float", {Atom(10.0)});
    h.send(vline, 0, "float", {Atom(1.0)});
    auto out = renderAll(h, 10);
    CHECK_THAT(out[0], WithinAbs(1.0 / 480, 1e-5));
    CHECK_THAT(out[239], WithinAbs(0.5, 1e-4));
    CHECK_THAT(out[479], WithinAbs(1.0, 1e-5));
    CHECK_THAT(out[600], WithinAbs(1.0, 1e-9));

    // Jump to 5 after 1.01 ms: lands on sample 48 of the next block.
    h.send(vline, 2, "float", {Atom(1.01)});
    h.send(vline, 0, "float", {Atom(5.0)});
    out = renderAll(h, 1);
    CHECK_THAT(out[47], WithinAbs(1.0, 1e-9));
    CHECK_THAT(out[48], WithinAbs(5.0, 1e-9));

    h.send(vline, 1, "float", {Atom(100.0)});
    h.send(vline, 0, "float", {Atom(0.0)});
    renderAll(h, 2);
    h.send(vline, 0, "stop");
    out = renderAll(h, 2);
    CHECK(out.front() == out.back());
    CHECK(out.back() < 5.0f);
    CHECK(out.back() > 4.0f);
}

TEST_CASE("lrshift~ shifts within the block", "[filter]") {
    Harness h;
    auto& c = h.open("#N canvas 0 0 400 300 12;\n" + graphArray("dirac", {0, 0, 1, 0}) + R"(#X obj 10 200 tabplay~ dirac;
#X obj 10 230 lrshift~ 1;
#X obj 10 260 dac~;
#X connect 1 0 2 0;
#X connect 2 0 3 0;
)");
    h.dsp(true);
    h.send(Harness::find(c, "tabplay~"), 0, "bang");
    const auto out = renderAll(h, 1);
    CHECK(peakIndex(out) == 1);
}

// ---------------------------------------------------------------------------
// Delays

TEST_CASE("delread~ delays an impulse by whole samples", "[delay]") {
    Harness h;
    auto& c = h.open("#N canvas 0 0 400 300 12;\n" + graphArray("dirac", {1}) + R"(#X obj 10 200 tabplay~ dirac;
#X obj 10 230 delwrite~ d1 100;
#X obj 100 200 delread~ d1 2;
#X obj 100 230 dac~;
#X connect 1 0 2 0;
#X connect 3 0 4 0;
)");
    h.dsp(true);
    h.send(Harness::find(c, "tabplay~"), 0, "bang");
    auto out = renderAll(h, 4);
    CHECK(peakIndex(out) == 96); // 2 ms at 48 kHz
    CHECK_THAT(out[96], WithinAbs(1.0, 1e-9));

    // Changing the delay time takes effect immediately.
    h.send(Harness::find(c, "delwrite~"), 0, "clear");
    h.send(Harness::find(c, "delread~"), 0, "float", {Atom(10.0)});
    h.send(Harness::find(c, "tabplay~"), 0, "bang");
    out = renderAll(h, 10);
    CHECK(peakIndex(out) == 480);

    // "clear" empties the line.
    h.send(Harness::find(c, "tabplay~"), 0, "bang");
    renderAll(h, 1);
    h.send(Harness::find(c, "delwrite~"), 0, "clear");
    out = renderAll(h, 10);
    CHECK(std::fabs(out[peakIndex(out)]) == 0.0f);
}

TEST_CASE("delread~ reading before the writer delays at least one block", "[delay]") {
    Harness h;
    // No connection orders delwrite~ and delread~: here the reader is
    // scheduled first, so (as in Pd) the minimum delay is one block.
    auto& c = h.open("#N canvas 0 0 400 300 12;\n" + graphArray("dirac", {1}) + R"(#X obj 10 200 tabplay~ dirac;
#X obj 10 230 delwrite~ d2 100;
#X obj 100 200 delread~ d2 0.5;
#X obj 100 230 dac~;
#X connect 1 0 2 0;
#X connect 3 0 4 0;
)");
    h.dsp(true);
    h.send(Harness::find(c, "tabplay~"), 0, "bang");
    const auto out = renderAll(h, 4);
    CHECK(peakIndex(out) == 64);
}

TEST_CASE("delread4~ interpolates sub-block delays after the writer", "[delay]") {
    Harness h;
    auto& c = h.open("#N canvas 0 0 400 300 12;\n" + graphArray("dirac", {1}) + R"(#X obj 10 200 tabplay~ dirac;
#X obj 10 230 delwrite~ d3 100;
#X obj 100 200 sig~ 0.5;
#X obj 100 230 vd~ d3;
#X obj 100 260 dac~;
#X connect 1 0 2 0;
#X connect 3 0 4 0;
#X connect 4 0 5 0;
)");
    h.dsp(true);
    h.send(Harness::find(c, "tabplay~"), 0, "bang");
    auto out = renderAll(h, 4);
    CHECK(peakIndex(out) == 24); // 0.5 ms
    CHECK_THAT(out[24], WithinAbs(1.0, 1e-6));
    CHECK_THAT(out[23], WithinAbs(0.0, 1e-6));

    // Half a sample more: the impulse is spread by the interpolator.
    h.send(Harness::find(c, "sig~"), 0, "float", {Atom(0.5 + 0.5 / 48.0)});
    h.send(Harness::find(c, "tabplay~"), 0, "bang");
    out = renderAll(h, 4);
    CHECK_THAT(out[24], WithinAbs(out[25], 1e-6));
    CHECK(out[24] > 0.5f);
}

// ---------------------------------------------------------------------------
// Tables

TEST_CASE("#X array contents load, read back and save", "[table]") {
    Harness h;
    auto& c = h.open("#N canvas 0 0 400 300 12;\n" + graphArray("arr", {1, 2, 3, 4}) + R"(#X obj 10 200 tabread arr;
#X obj 10 230 print;
#X connect 1 0 2 0;
)");
    Box* tr = Harness::find(c, "tabread");
    h.send(tr, 0, "float", {Atom(2.0)});
    h.send(tr, 0, "float", {Atom(-5.0)});
    h.send(tr, 0, "float", {Atom(99.0)});
    CHECK(h.take() == std::vector<std::string>{"print: 3", "print: 1", "print: 4"});

    const auto text = writePatch(c);
    CHECK(text.find("#X array arr 4 float 3;\n#A 0 1 2 3 4;\n") != std::string::npos);

    // The GUI view holds the contents before audio ever ran.
    const SnapshotBuffer* snap = snapshotOf(c, "arr");
    REQUIRE(snap);
    std::vector<float> view;
    std::uint64_t seen = 0;
    REQUIRE(snap->read(view, seen));
    CHECK(view == std::vector<float>{1, 2, 3, 4});
}

TEST_CASE("arrays without the save flag do not save contents", "[table]") {
    Harness h;
    auto& c = h.open(R"(#N canvas 0 0 400 300 12;
#N canvas 0 0 450 300 (subpatch) 0;
#X array nosave 3 float 2;
#X coords 0 1 3 -1 200 140 1 0 0;
#X restore 10 10 graph;
)");
    const auto text = writePatch(c);
    CHECK(text.find("#X array nosave 3 float 2;") != std::string::npos);
    CHECK(text.find("#A") == std::string::npos);
}

TEST_CASE("messages to the array name edit it", "[table]") {
    Harness h;
    auto& c = h.open("#N canvas 0 0 400 300 12;\n" + graphArray("arr", {0, 0, 0, 0}) + R"(#X obj 10 200 tabread arr;
#X obj 10 230 print;
#X msg 100 100 \; arr 1 5 6;
#X msg 100 130 \; arr const 7;
#X msg 100 160 \; arr resize 8 \; arr 7 9;
#X msg 100 190 \; arr normalize 2;
#X connect 1 0 2 0;
)");
    Box* tr = Harness::find(c, "tabread");
    auto read = [&](double i) {
        h.send(tr, 0, "float", {Atom(i)});
        auto log = h.take();
        REQUIRE(log.size() == 1);
        return std::stod(log[0].substr(7));
    };
    auto click = [&](int index) { h.send(c.boxes()[static_cast<std::size_t>(index)].get(), 0, "bang"); };

    click(3);
    h.run();
    CHECK(read(0) == 0);
    CHECK(read(1) == 5);
    CHECK(read(2) == 6);
    CHECK(read(3) == 0);

    click(4);
    h.run();
    CHECK(read(3) == 7);

    click(5);
    h.run();
    CHECK(read(7) == 9);
    CHECK(read(6) == 0);

    click(6);
    h.run();
    CHECK(read(7) == 2);
    CHECK_THAT(read(0), WithinAbs(14.0 / 9.0, 1e-5));

    // After the refresh period the GUI view and the saved contents follow.
    h.run(30);
    const SnapshotBuffer* snap = snapshotOf(c, "arr");
    std::vector<float> view;
    std::uint64_t seen = 0;
    REQUIRE(snap->read(view, seen));
    CHECK(view.size() == 8);
    CHECK(view[7] == 2.0f);
    const auto saved = writePatch(c);
    INFO(saved);
    CHECK(saved.find("#A 0 1.55556 1.55556 1.55556 1.55556 0 0 0 2;") != std::string::npos);

    // The box text still says 4 points; loading the saved contents grows
    // the array so the resize survives a save/load round trip.
    Harness h2;
    auto& c2 = h2.open(saved);
    Box* tr2 = Harness::find(c2, "tabread");
    h2.send(tr2, 0, "float", {Atom(7.0)});
    CHECK(h2.take() == std::vector<std::string>{"print: 2"});
}

TEST_CASE("tabread4~ interpolates cubics exactly", "[table]") {
    Harness h;
    std::vector<float> squares;
    for (int i = 0; i < 8; ++i)
        squares.push_back(static_cast<float>(i * i));
    auto& c = h.open("#N canvas 0 0 400 300 12;\n" + graphArray("sq", squares) + R"(#X obj 10 200 sig~ 2.5;
#X obj 10 230 tabread4~ sq;
#X obj 10 260 dac~;
#X obj 100 200 tabread4 sq;
#X obj 100 230 print;
#X connect 1 0 2 0;
#X connect 2 0 3 0;
#X connect 4 0 5 0;
)");
    h.dsp(true);
    auto out = renderAll(h, 1);
    CHECK_THAT(out[0], WithinAbs(6.25, 1e-5));

    // The onset inlet adds to the index; out of range clamps.
    h.send(Harness::find(c, "tabread4~"), 1, "float", {Atom(1.25)});
    out = renderAll(h, 1);
    CHECK_THAT(out[0], WithinAbs(3.75 * 3.75, 1e-4));
    h.send(Harness::find(c, "sig~"), 0, "float", {Atom(100.0)});
    out = renderAll(h, 1);
    CHECK_THAT(out[0], WithinAbs(36.0, 1e-5)); // last valid point is index 6

    h.send(Harness::find(c, "tabread4"), 0, "float", {Atom(3.5)});
    CHECK(h.take() == std::vector<std::string>{"print: 12.25"});
}

TEST_CASE("tabread~ truncates and clamps", "[table]") {
    Harness h;
    h.open("#N canvas 0 0 400 300 12;\n" + graphArray("t", {10, 20, 30}) + R"(#X obj 10 200 sig~ 1.9;
#X obj 10 230 tabread~ t;
#X obj 10 260 dac~;
#X connect 1 0 2 0;
#X connect 2 0 3 0;
)");
    h.dsp(true);
    CHECK_THAT(renderAll(h, 1)[0], WithinAbs(20.0, 1e-9));
}

TEST_CASE("tabwrite~ records a signal into a table", "[table]") {
    Harness h;
    auto& c = h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 table rec 100;
#X obj 10 40 sig~ 0.5;
#X obj 10 70 tabwrite~ rec;
#X obj 100 100 tabread rec;
#X obj 100 130 print;
#X connect 1 0 2 0;
#X connect 3 0 4 0;
)");
    h.dsp(true);
    Box* tr = Harness::find(c, "tabread");
    h.send(tr, 0, "float", {Atom(99.0)});
    CHECK(h.take() == std::vector<std::string>{"print: 0"});

    h.send(Harness::find(c, "tabwrite~"), 0, "start", {Atom(10.0)});
    renderAll(h, 3);
    h.send(tr, 0, "float", {Atom(9.0)});
    h.send(tr, 0, "float", {Atom(10.0)});
    h.send(tr, 0, "float", {Atom(99.0)});
    CHECK(h.take() == std::vector<std::string>{"print: 0", "print: 0.5", "print: 0.5"});

    // [table] contents are not saved with the patch.
    const auto* t = dynamic_cast<const TableStorage*>(Harness::find(c, "table")->object.get());
    REQUIRE(t);
    CHECK_FALSE(t->tableSaveContents());
}

TEST_CASE("tabplay~ plays a table and bangs at the end", "[table]") {
    Harness h;
    auto& c = h.open("#N canvas 0 0 400 300 12;\n" + graphArray("s", {1, 2, 3, 4, 5}) + R"(#X obj 10 200 tabplay~ s;
#X obj 10 230 dac~;
#X obj 100 230 print done;
#X connect 1 0 2 0;
#X connect 1 1 3 0;
)");
    h.dsp(true);
    Box* play = Harness::find(c, "tabplay~");
    h.send(play, 0, "bang");
    auto out = renderAll(h, 2);
    CHECK(std::vector<float>(out.begin(), out.begin() + 6) == std::vector<float>{1, 2, 3, 4, 5, 0});
    CHECK(h.log == std::vector<std::string>{"done: bang"});
    h.log.clear();

    h.send(play, 0, "list", {Atom(1.0), Atom(2.0)});
    out = renderAll(h, 1);
    CHECK(std::vector<float>(out.begin(), out.begin() + 3) == std::vector<float>{2, 3, 0});

    h.send(play, 0, "float", {Atom(3.0)});
    out = renderAll(h, 1);
    CHECK(std::vector<float>(out.begin(), out.begin() + 3) == std::vector<float>{4, 5, 0});
}

TEST_CASE("tabosc4~ plays a sinesum table", "[table]") {
    Harness h;
    auto& c = h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 table wave;
#X msg 10 40 \; wave sinesum 64 1;
#X obj 10 70 tabosc4~ wave;
#X obj 10 100 dac~;
#X obj 100 100 array size wave;
#X obj 100 130 print size;
#X connect 2 0 3 0;
#X connect 4 0 5 0;
)");
    h.dsp(true);
    h.send(c.boxes()[1].get(), 0, "bang");
    h.send(Harness::find(c, "array"), 0, "bang");
    CHECK(h.take() == std::vector<std::string>{"size: 67"});

    h.send(Harness::find(c, "tabosc4~"), 0, "float", {Atom(750.0)}); // one period per block
    const auto out = renderAll(h, 3);
    const std::vector<float> last(out.end() - 64, out.end());
    CHECK_THAT(*std::ranges::max_element(last), WithinAbs(1.0, 1e-3));
    CHECK_THAT(*std::ranges::min_element(last), WithinAbs(-1.0, 1e-3));
    for (std::size_t i = 0; i < 64; ++i)
        CHECK_THAT(out[out.size() - 128 + i], WithinAbs(last[i], 1e-4));
}

TEST_CASE("array define with size/get/set/sum/max", "[table]") {
    Harness h;
    auto& c = h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 array define a1 5;
#X obj 10 40 array set a1 1;
#X obj 10 70 array get a1;
#X obj 10 100 print get;
#X obj 100 70 array size a1;
#X obj 100 100 print size;
#X obj 200 70 array sum a1;
#X obj 200 100 print sum;
#X obj 300 70 array max a1;
#X obj 300 100 print max;
#X obj 350 100 print maxi;
#X connect 2 0 3 0;
#X connect 4 0 5 0;
#X connect 6 0 7 0;
#X connect 8 0 9 0;
#X connect 8 1 10 0;
)");
    h.send(Harness::find(c, "array", 1), 0, "list", {Atom(1.0), Atom(4.0), Atom(2.0)});
    h.send(Harness::find(c, "array", 2), 0, "bang");
    h.send(Harness::find(c, "array", 3), 0, "bang");
    h.send(Harness::find(c, "array", 4), 0, "bang");
    h.send(Harness::find(c, "array", 5), 0, "bang");
    CHECK(h.take() == std::vector<std::string>{"get: 0 1 4 2 0", "size: 5", "sum: 7", "maxi: 2", "max: 4"});

    // get with onset and count; size with a float resizes.
    h.send(Harness::find(c, "array", 2), 1, "float", {Atom(2.0)});
    h.send(Harness::find(c, "array", 2), 0, "float", {Atom(2.0)});
    h.send(Harness::find(c, "array", 3), 0, "float", {Atom(2.0)});
    h.send(Harness::find(c, "array", 3), 0, "bang");
    CHECK(h.take() == std::vector<std::string>{"get: 4 2", "size: 2"});
}

TEST_CASE("table users follow tables that appear and disappear", "[table]") {
    Harness h;
    auto& c = h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 tabreceive~ late;
#X obj 10 40 dac~;
#X obj 100 10 tabread late;
#X obj 100 40 print;
#X connect 0 0 1 0;
#X connect 2 0 3 0;
)");
    h.dsp(true);
    CHECK_THAT(renderAll(h, 1)[0], WithinAbs(0.0, 1e-9));
    h.send(Harness::find(c, "tabread"), 0, "float", {Atom(0.0)});
    CHECK(h.take() == std::vector<std::string>{"tabread: late: no such array"});

    auto& t = c.addBox(BoxKind::Object, 200, 10, parseAtoms("table late 64"));
    h.rt.commit();
    h.rt.engine().sendToName(Symbol("late"), sym::list(), std::vector<Atom>{Atom(0.0), Atom(0.25), Atom(0.75)});
    auto out = renderAll(h, 1);
    CHECK_THAT(out[0], WithinAbs(0.25, 1e-9));
    CHECK_THAT(out[1], WithinAbs(0.75, 1e-9));

    c.removeBox(t);
    h.rt.commit();
    out = renderAll(h, 1);
    CHECK_THAT(out[0], WithinAbs(0.0, 1e-9));
}
