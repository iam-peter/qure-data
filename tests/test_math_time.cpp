// Math ([+] ... [expr] [makefilename]) and time ([metro] [delay] [timer]
// [pipe] [line]) objects, compared against Pd. One block is 64 samples at
// 48 kHz = 4/3 ms of logical time.

#include "Harness.hpp"

#include <clocale>
#include <string>

using namespace qure;
using namespace qure::test;

namespace {

using Lines = std::vector<std::string>;

/// [obj] with a [print N] on every outlet N.
struct Probe {
    Harness h;
    Canvas* c = nullptr;
    Box* obj = nullptr;

    explicit Probe(std::string_view text) {
        c = &h.rt.newPatch();
        obj = &c->addBox(BoxKind::Object, 0, 0, parseAtoms(text));
        REQUIRE_FALSE(obj->broken());
        for (int o = 0; o < obj->numOutlets(); ++o) {
            auto& p = c->addBox(BoxKind::Object, 0, 40, parseAtoms("print " + std::to_string(o)));
            c->connect(*obj, o, p, 0);
        }
        h.rt.commit();
        h.run();
    }
    /// Sends a message (delivered at the next block) and returns what was
    /// printed during `blocks` blocks.
    Lines send(int inlet, std::string_view sel, std::vector<Atom> args = {}, int blocks = 1) {
        h.send(obj, inlet, sel, std::move(args));
        return h.take(blocks);
    }
    Lines f(double v, int inlet = 0) { return send(inlet, "float", {Atom(v)}); }
    Lines wait(int blocks) { return h.take(blocks); }
};

Atom s(std::string_view text) { return Atom(Symbol(text)); }

bool creates(std::string_view text) {
    Harness h;
    auto& c = h.rt.newPatch();
    auto& b = c.addBox(BoxKind::Object, 0, 0, parseAtoms(text));
    return !b.broken();
}

} // namespace

// ===========================================================================
// Binary operators

TEST_CASE("binops: right inlet is cold, creation arg sets it", "[math]") {
    Probe p("+ 5");
    CHECK(p.f(1) == Lines{"0: 6"});
    CHECK(p.f(10, 1).empty());
    CHECK(p.send(0, "bang") == Lines{"0: 11"});
    CHECK(p.f(2) == Lines{"0: 12"});
}

TEST_CASE("binops: a list into the left inlet is distributed", "[math]") {
    Probe p("-");
    CHECK(p.send(0, "list", {Atom(10.0), Atom(3.0)}) == Lines{"0: 7"});
    CHECK(p.send(0, "bang") == Lines{"0: 7"});
}

TEST_CASE("binops: division and pow edge cases", "[math]") {
    Probe div0("/ 0");
    CHECK(div0.f(5) == Lines{"0: 0"});
    Probe half("/ 2");
    CHECK(half.f(1) == Lines{"0: 0.5"});

    Probe sq("pow 2");
    CHECK(sq.f(-3) == Lines{"0: 9"});
    Probe root("pow 0.5");
    CHECK(root.f(-4) == Lines{"0: 0"}); // negative base, fractional exponent
    CHECK(root.f(16) == Lines{"0: 4"});
    Probe inv("pow -1");
    CHECK(inv.f(0) == Lines{"0: 0"}); // 0 to a negative power
}

TEST_CASE("binops: [mod] [div] and [%] with negative numbers", "[math]") {
    Probe mod("mod 3");
    CHECK(mod.f(7) == Lines{"0: 1"});
    CHECK(mod.f(-1) == Lines{"0: 2"});
    CHECK(mod.f(-3) == Lines{"0: 0"});
    Probe modNeg("mod -3");
    CHECK(modNeg.f(-1) == Lines{"0: 2"});
    Probe mod0("mod 0");
    CHECK(mod0.f(5) == Lines{"0: 0"});

    Probe div("div 3");
    CHECK(div.f(7) == Lines{"0: 2"});
    CHECK(div.f(-1) == Lines{"0: -1"});
    CHECK(div.f(-3) == Lines{"0: -1"});
    CHECK(div.f(-4) == Lines{"0: -2"});
    CHECK(div.f(3.9) == Lines{"0: 1"});

    Probe rem("% 3");
    CHECK(rem.f(-4) == Lines{"0: -1"});
    CHECK(rem.f(7.5) == Lines{"0: 1"});
    Probe rem0("% 0");
    CHECK(rem0.f(5) == Lines{"0: 0"});
}

TEST_CASE("binops: comparison, logic and bit operators", "[math]") {
    CHECK(Probe("== 2").f(2) == Lines{"0: 1"});
    CHECK(Probe("!= 2").f(2) == Lines{"0: 0"});
    CHECK(Probe("> 2").f(3) == Lines{"0: 1"});
    CHECK(Probe("< 2").f(3) == Lines{"0: 0"});
    CHECK(Probe(">= 2").f(2) == Lines{"0: 1"});
    CHECK(Probe("<= 2").f(2.5) == Lines{"0: 0"});
    CHECK(Probe("&& 1").f(0.5) == Lines{"0: 0"}); // integer parts, like Pd
    CHECK(Probe("|| 0").f(2) == Lines{"0: 1"});
    CHECK(Probe("& 6").f(3) == Lines{"0: 2"});
    CHECK(Probe("| 6").f(3) == Lines{"0: 7"});
    CHECK(Probe("<< 3").f(1) == Lines{"0: 8"});
    CHECK(Probe(">> 1").f(-8) == Lines{"0: -4"});
    CHECK(Probe("max 3").f(5) == Lines{"0: 5"});
    CHECK(Probe("min 3").f(5) == Lines{"0: 3"});
}

TEST_CASE("unary math functions", "[math]") {
    CHECK(Probe("abs").f(-3) == Lines{"0: 3"});
    CHECK(Probe("sqrt").f(16) == Lines{"0: 4"});
    CHECK(Probe("sqrt").f(-1) == Lines{"0: 0"});
    CHECK(Probe("exp").f(0) == Lines{"0: 1"});
    CHECK(Probe("log").f(1) == Lines{"0: 0"});
    CHECK(Probe("log").f(0) == Lines{"0: -1000"});
    CHECK(Probe("log 10").f(1000) == Lines{"0: 3"});
    CHECK(Probe("sin").f(0) == Lines{"0: 0"});
    CHECK(Probe("cos").f(0) == Lines{"0: 1"});
    CHECK(Probe("tan").f(0) == Lines{"0: 0"});
    CHECK(Probe("atan").f(1) == Lines{"0: 0.785398"});
    CHECK(Probe("wrap").f(-0.25) == Lines{"0: 0.75"});
    CHECK(Probe("wrap").f(1.5) == Lines{"0: 0.5"});
    CHECK(Probe("mtof").f(69) == Lines{"0: 440"});
    CHECK(Probe("mtof").f(-1500) == Lines{"0: 0"});
    CHECK(Probe("ftom").f(440) == Lines{"0: 69"});
    CHECK(Probe("ftom").f(0) == Lines{"0: -1500"});
    CHECK(Probe("dbtorms").f(100) == Lines{"0: 1"});
    CHECK(Probe("dbtorms").f(0) == Lines{"0: 0"});
    CHECK(Probe("rmstodb").f(1) == Lines{"0: 100"});
    CHECK(Probe("rmstodb").f(0.1) == Lines{"0: 80"});
    CHECK(Probe("powtodb").f(0.01) == Lines{"0: 80"});
    CHECK(Probe("dbtopow").f(80) == Lines{"0: 0.01"});
}

TEST_CASE("[atan2] and [clip]", "[math]") {
    Probe a("atan2");
    CHECK(a.send(0, "list", {Atom(1.0), Atom(0.0)}) == Lines{"0: 1.5708"});
    CHECK(a.send(0, "list", {Atom(0.0), Atom(0.0)}) == Lines{"0: 0"});

    Probe c("clip 0 10");
    CHECK(c.f(15) == Lines{"0: 10"});
    CHECK(c.f(-1) == Lines{"0: 0"});
    CHECK(c.f(5) == Lines{"0: 5"});
    c.f(4, 2);
    CHECK(c.send(0, "bang") == Lines{"0: 4"});
}

TEST_CASE("[random] is in range and reproducible from a seed", "[math]") {
    Probe a("random 10");
    Probe b("random 10");
    a.send(0, "seed", {Atom(123.0)});
    b.send(0, "seed", {Atom(123.0)});
    for (int i = 0; i < 50; ++i) {
        const auto x = a.send(0, "bang");
        const auto y = b.send(0, "bang");
        REQUIRE(x.size() == 1);
        CHECK(x == y);
        const int v = std::stoi(x[0].substr(3));
        CHECK(v >= 0);
        CHECK(v < 10);
    }
    // The right inlet sets the range; ranges below 1 yield 0.
    a.f(0, 1);
    CHECK(a.send(0, "bang") == Lines{"0: 0"});
}

// ===========================================================================
// makefilename

TEST_CASE("[makefilename] formats numbers and symbols", "[math]") {
    CHECK(Probe("makefilename file%d.wav").f(3) == Lines{"0: symbol file3.wav"});
    CHECK(Probe("makefilename %03d").f(7) == Lines{"0: symbol 007"});
    CHECK(Probe("makefilename %i").f(-2.7) == Lines{"0: symbol -2"});
    CHECK(Probe("makefilename %f").f(1.5) == Lines{"0: symbol 1.500000"});
    CHECK(Probe("makefilename %.2f").f(1.5) == Lines{"0: symbol 1.50"});
    CHECK(Probe("makefilename %g").f(0.25) == Lines{"0: symbol 0.25"});
    CHECK(Probe("makefilename %x").f(255) == Lines{"0: symbol ff"});
    CHECK(Probe("makefilename %c").f(65) == Lines{"0: symbol A"});
    CHECK(Probe("makefilename 100%%-%d").f(5) == Lines{"0: symbol 100%-5"});
    CHECK(Probe("makefilename %s-x").f(1.5) == Lines{"0: symbol 1.5-x"});
    CHECK(Probe("makefilename %ld").f(12) == Lines{"0: symbol 12"});

    Probe sym("makefilename %s.txt");
    CHECK(sym.send(0, "symbol", {s("foo")}) == Lines{"0: symbol foo.txt"});
    Probe numFromSym("makefilename n%d");
    CHECK(numFromSym.send(0, "symbol", {s("foo")}) == Lines{"0: symbol n0"});

    Probe set("makefilename");
    CHECK(set.f(1) == Lines{"0: symbol file.1"});
    CHECK(set.send(0, "set", {s("take-%d")}).empty());
    CHECK(set.f(2) == Lines{"0: symbol take-2"});
}

TEST_CASE("[makefilename] and [expr] ignore a decimal-comma locale", "[math][expr]") {
    const std::string old = std::setlocale(LC_NUMERIC, nullptr);
    if (!std::setlocale(LC_NUMERIC, "de_DE.UTF-8") && !std::setlocale(LC_NUMERIC, "de_DE"))
        SKIP("no German locale installed");
    Probe f("makefilename a_%.1f_b");
    Probe e("expr 1.5 * 2.5");
    const auto fOut = f.f(1.5);
    const auto eOut = e.send(0, "bang");
    std::setlocale(LC_NUMERIC, old.c_str());
    CHECK(fOut == Lines{"0: symbol a_1.5_b"});
    CHECK(eOut == Lines{"0: 3.75"});
}

TEST_CASE("[makefilename] rejects unsafe or ambiguous formats", "[math]") {
    CHECK_FALSE(creates("makefilename %d%d"));
    CHECK_FALSE(creates("makefilename %n"));
    CHECK_FALSE(creates("makefilename %*d"));
    CHECK(creates("makefilename plain"));
}

// ===========================================================================
// expr

TEST_CASE("[expr] precedence and associativity follow C", "[expr]") {
    CHECK(Probe("expr 1 + 2 * 3").send(0, "bang") == Lines{"0: 7"});
    CHECK(Probe("expr (1 + 2) * 3").send(0, "bang") == Lines{"0: 9"});
    CHECK(Probe("expr 10 - 4 - 3").send(0, "bang") == Lines{"0: 3"});
    CHECK(Probe("expr 2 * 3 % 4").send(0, "bang") == Lines{"0: 2"});
    CHECK(Probe("expr 2 + 3 << 1").send(0, "bang") == Lines{"0: 10"});
    CHECK(Probe("expr 1 < 2 == 1").send(0, "bang") == Lines{"0: 1"});
    CHECK(Probe("expr 7 & 3 | 8").send(0, "bang") == Lines{"0: 11"});
    CHECK(Probe("expr 6 ^ 3 & 1").send(0, "bang") == Lines{"0: 7"});
    CHECK(Probe("expr 1 || 0 && 0").send(0, "bang") == Lines{"0: 1"});
    CHECK(Probe("expr -2 * -3").send(0, "bang") == Lines{"0: 6"});
    CHECK(Probe("expr !0 + 1").send(0, "bang") == Lines{"0: 2"});
    CHECK(Probe("expr ~0").send(0, "bang") == Lines{"0: -1"});
    CHECK(Probe("expr 2*3+4*5").send(0, "bang") == Lines{"0: 26"});
}

TEST_CASE("[expr] keeps Pd's int/float distinction", "[expr]") {
    CHECK(Probe("expr 1/2").send(0, "bang") == Lines{"0: 0"});
    CHECK(Probe("expr 1.5/2").send(0, "bang") == Lines{"0: 0.75"});
    CHECK(Probe("expr 7/0").send(0, "bang") == Lines{"0: 0"});
    CHECK(Probe("expr $f1/2").f(1) == Lines{"0: 0.5"});
    CHECK(Probe("expr $i1/2").f(3.7) == Lines{"0: 1"});
    CHECK(Probe("expr -7 % 3").send(0, "bang") == Lines{"0: -1"});
    CHECK(Probe("expr float(1)/2").send(0, "bang") == Lines{"0: 0.5"});
}

TEST_CASE("[expr] inputs: inlets, cold inlets and lists", "[expr]") {
    Probe p("expr $f1 + $f3 * 10");
    CHECK(p.obj->numInlets() == 3);
    CHECK(p.f(2, 2).empty());
    CHECK(p.f(1) == Lines{"0: 21"});
    CHECK(p.send(0, "list", {Atom(5.0), Atom(0.0), Atom(1.0)}) == Lines{"0: 15"});
    CHECK(p.send(0, "bang") == Lines{"0: 15"});
}

TEST_CASE("[expr] several expressions output right to left", "[expr]") {
    Probe p("expr $f1 + 1; $f1 * 2; $f2");
    CHECK(p.obj->numOutlets() == 3);
    p.f(7, 1);
    CHECK(p.f(5) == Lines{"2: 7", "1: 10", "0: 6"});
}

TEST_CASE("[expr] functions", "[expr]") {
    CHECK(Probe("expr min($f1, 3)").f(5) == Lines{"0: 3"});
    CHECK(Probe("expr max($f1, 3)").f(5) == Lines{"0: 5"});
    CHECK(Probe("expr abs(-2.5)").send(0, "bang") == Lines{"0: 2.5"});
    CHECK(Probe("expr int(3.7) + int(-3.7)").send(0, "bang") == Lines{"0: 0"});
    CHECK(Probe("expr sqrt(16)").send(0, "bang") == Lines{"0: 4"});
    CHECK(Probe("expr pow(2, 10)").send(0, "bang") == Lines{"0: 1024"});
    CHECK(Probe("expr exp(0) + log(1) + log10(100)").send(0, "bang") == Lines{"0: 3"});
    CHECK(Probe("expr floor(-1.5)").send(0, "bang") == Lines{"0: -2"});
    CHECK(Probe("expr ceil(1.2)").send(0, "bang") == Lines{"0: 2"});
    CHECK(Probe("expr fmod(7.5, 2)").send(0, "bang") == Lines{"0: 1.5"});
    CHECK(Probe("expr rint(2.6)").send(0, "bang") == Lines{"0: 3"});
    CHECK(Probe("expr atan2(1, 0)").send(0, "bang") == Lines{"0: 1.5708"});
    CHECK(Probe("expr sin(0) + cos(0) + tan(0) + asin(0) + acos(1) + atan(0)").send(0, "bang") == Lines{"0: 1"});
    CHECK(Probe("expr mtof(69)").send(0, "bang") == Lines{"0: 440"});
    CHECK(Probe("expr if($f1 > 0, 1, -1)").f(3) == Lines{"0: 1"});
    CHECK(Probe("expr if($f1 > 0, 1, -1)").f(-3) == Lines{"0: -1"});
    CHECK(Probe("expr min(max($f1, 0), 10) * 2").f(20) == Lines{"0: 20"});

    Probe r("expr random(5, 8)");
    for (int i = 0; i < 20; ++i) {
        const auto out = r.send(0, "bang");
        REQUIRE(out.size() == 1);
        const int v = std::stoi(out[0].substr(3));
        CHECK(v >= 5);
        CHECK(v < 8);
    }
}

TEST_CASE("[expr] from a patch file with escaped commas and semicolons", "[expr]") {
    Harness h;
    auto& c = h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 expr min(\$f1 \, 4) \; \$f1 * 2;
#X obj 10 40 print a;
#X obj 90 40 print b;
#X connect 0 0 1 0;
#X connect 0 1 2 0;
)");
    Box* e = Harness::find(c, "expr");
    REQUIRE(e);
    REQUIRE_FALSE(e->broken());
    h.send(e, 0, "float", {Atom(9.0)});
    CHECK(h.take() == Lines{"b: 18", "a: 4"});
}

TEST_CASE("[expr] reports syntax errors at creation", "[expr]") {
    CHECK_FALSE(creates("expr 1 +"));
    CHECK_FALSE(creates("expr (1"));
    CHECK_FALSE(creates("expr foo(1)"));
    CHECK_FALSE(creates("expr min(1)"));
    CHECK_FALSE(creates("expr $f0"));
    CHECK_FALSE(creates("expr 1 2"));
    CHECK(creates("expr $f9"));
}

// ===========================================================================
// Time

TEST_CASE("[metro] bangs at its interval in logical time", "[time]") {
    Probe m("metro 10");
    // Started at t = 4/3 ms; the run ends at 75 * 4/3 = 100 ms, so bangs
    // fall at 1.33, 11.33, ..., 91.33.
    const auto out = m.send(0, "bang", {}, 74);
    CHECK(out.size() == 10);
    CHECK(m.send(0, "stop", {}, 30).empty());
    // A nonzero float starts, zero stops.
    CHECK(m.f(1).size() == 1);
    CHECK(m.send(0, "float", {Atom(0.0)}, 30).empty());
    // The right inlet changes the interval: 25 ms over 93 ms.
    m.f(25, 1);
    CHECK(m.send(0, "bang", {}, 70).size() == 4);
}

TEST_CASE("[metro] is drift free", "[time]") {
    Harness h;
    auto& c = h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 metro 10;
#X obj 10 40 t b b;
#X obj 10 70 timer;
#X obj 10 100 print;
#X connect 0 0 1 0;
#X connect 1 0 2 0;
#X connect 1 1 2 1;
#X connect 2 0 3 0;
)");
    h.run();
    h.send(Harness::find(c, "metro"), 0, "bang");
    // Blocks 1..7499 cover 4/3 ms .. 10 s: the metro started at 4/3 ms, so
    // it fires 1000 times, the timer measuring exactly 10 ms between bangs.
    const auto out = h.take(7499);
    CHECK(out.front() == "print: 1.33333");
    REQUIRE(out.size() == 1000);
    for (std::size_t i = 1; i < out.size(); ++i)
        REQUIRE(out[i] == "print: 10");
}

TEST_CASE("[metro] tempo message changes the unit", "[time]") {
    Probe m("metro 1");
    m.send(0, "tempo", {Atom(20.0), s("msec")});
    // One bang every 20 ms over 98.7 ms.
    CHECK(m.send(0, "bang", {}, 74).size() == 5);
    // Creation arguments: 1 beat at 600 per minute = 100 ms.
    Probe beat("metro 1 600 permin");
    CHECK(beat.send(0, "bang", {}, 140).size() == 2);
}

TEST_CASE("[delay] schedules, reschedules and stops", "[time]") {
    Probe d("delay 100");
    CHECK(d.send(0, "bang", {}, 74).empty()); // t = 1.33 .. 100
    CHECK(d.wait(2) == Lines{"0: bang"});
    // A float sets the time and starts.
    CHECK(d.f(10).empty());
    CHECK(d.wait(8) == Lines{"0: bang"});
    // Rescheduling replaces the pending bang.
    d.f(100, 1);
    d.send(0, "bang", {}, 30);
    CHECK(d.send(0, "bang", {}, 70).empty());
    CHECK(d.wait(10) == Lines{"0: bang"});
    // stop
    d.send(0, "bang");
    CHECK(d.send(0, "stop", {}, 100).empty());
    // The right inlet sets the time without starting.
    CHECK(d.f(5, 1).empty());
    CHECK(d.send(0, "bang", {}, 5) == Lines{"0: bang"});
}

TEST_CASE("[del] with a tempo unit", "[time]") {
    Probe d("del 0.05 1 sec"); // 50 ms
    CHECK(d.send(0, "bang", {}, 36).empty());
    CHECK(d.wait(3) == Lines{"0: bang"});
}

TEST_CASE("[timer] measures logical time", "[time]") {
    Probe t("timer");
    t.send(0, "bang");                                   // reset at t = 4/3
    CHECK(t.send(1, "bang", {}, 1) == Lines{"0: 1.33333"}); // read at 8/3
    t.wait(28);
    CHECK(t.send(1, "bang") == Lines{"0: 40"});         // 30 blocks later
    Probe sec("timer 1 sec");
    sec.send(0, "bang");
    sec.wait(749);
    CHECK(sec.send(1, "bang") == Lines{"0: 1"});
}

TEST_CASE("[pipe] delays numbers in time order", "[time]") {
    Probe p("pipe 50");
    CHECK(p.f(1).empty()); // due at 51.33
    p.f(10, 1);
    CHECK(p.f(2).empty()); // due at 14
    CHECK(p.wait(20) == Lines{"0: 2"});
    CHECK(p.wait(20) == Lines{"0: 1"});
    // A list's extra element sets the delay.
    CHECK(p.send(0, "list", {Atom(3.0), Atom(5.0)}, 3).empty());
    CHECK(p.wait(2) == Lines{"0: 3"});
}

TEST_CASE("[pipe] equal due times keep arrival order", "[time]") {
    Probe p("pipe 10");
    Harness& h = p.h;
    h.send(p.obj, 0, "float", {Atom(1.0)});
    h.send(p.obj, 0, "float", {Atom(2.0)});
    h.send(p.obj, 0, "float", {Atom(3.0)});
    CHECK(h.take(10) == Lines{"0: 1", "0: 2", "0: 3"});
}

TEST_CASE("[pipe] with symbols, flush and clear", "[time]") {
    Probe p("pipe f s 20");
    CHECK(p.obj->numInlets() == 3);
    CHECK(p.obj->numOutlets() == 2);
    CHECK(p.send(0, "list", {Atom(1.0), s("foo")}).empty());
    CHECK(p.wait(20) == Lines{"1: symbol foo", "0: 1"});

    // flush outputs everything pending at once, newest first (as Pd does).
    p.send(0, "list", {Atom(1.0), s("a")});
    p.send(0, "list", {Atom(2.0), s("b")});
    CHECK(p.send(0, "flush") == Lines{"1: symbol b", "0: 2", "1: symbol a", "0: 1"});
    CHECK(p.wait(30).empty());

    p.send(0, "list", {Atom(3.0), s("c")});
    CHECK(p.send(0, "clear").empty());
    CHECK(p.wait(30).empty());

    // A bang re-sends the stored values.
    CHECK(p.send(0, "bang", {}, 20) == Lines{"1: symbol c", "0: 3"});
}

TEST_CASE("[pipe] holds many pending events", "[time]") {
    Probe p("pipe 1000");
    Harness& h = p.h;
    for (int i = 0; i < 600; ++i)
        h.send(p.obj, 0, "float", {Atom(static_cast<double>(i))});
    const auto out = h.take(800);
    REQUIRE(out.size() == 600);
    CHECK(out.front() == "0: 0");
    CHECK(out.back() == "0: 599");
}

TEST_CASE("[line] ramps with the grain", "[time]") {
    Probe l("line");
    // 0 → 10 in 100 ms, output every 20 ms from t = 4/3.
    CHECK(l.send(0, "list", {Atom(10.0), Atom(100.0)}, 80) ==
          Lines{"0: 0", "0: 2", "0: 4", "0: 6", "0: 8", "0: 10"});
    // Without a time, a float jumps.
    CHECK(l.f(3) == Lines{"0: 3"});
    // A custom grain (third inlet).
    l.f(50, 2);
    CHECK(l.send(0, "list", {Atom(13.0), Atom(100.0)}, 80) == Lines{"0: 3", "0: 8", "0: 13"});
}

TEST_CASE("[line] grain argument, set and stop", "[time]") {
    Probe l("line 0 25");
    CHECK(l.send(0, "list", {Atom(100.0), Atom(100.0)}, 80) ==
          Lines{"0: 0", "0: 25", "0: 50", "0: 75", "0: 100"});
    l.send(0, "set", {Atom(0.0)});
    l.send(0, "list", {Atom(100.0), Atom(100.0)}, 30); // 0, 25 (at 25 ms)
    CHECK(l.send(0, "stop", {}, 80).empty());
    // Stopped 30 blocks (40 ms) into the ramp, the line holds at 40 and a
    // new ramp starts from there.
    CHECK(l.send(0, "list", {Atom(29.0), Atom(10.0)}, 20) == Lines{"0: 40", "0: 29"});
}

TEST_CASE("[cputime] and [realtime] report elapsed milliseconds", "[time]") {
    for (const char* cls : {"cputime", "realtime"}) {
        Probe p(cls);
        p.send(0, "bang");
        const auto out = p.send(1, "bang");
        REQUIRE(out.size() == 1);
        CHECK(std::stod(out[0].substr(3)) >= 0);
    }
}
