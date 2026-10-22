#include "Harness.hpp"
#include "PdFile.hpp"

#include <atomic>
#include <fstream>
#include <thread>

using namespace qure;
using namespace qure::test;

TEST_CASE("loadbang through a message box to print", "[engine]") {
    Harness h;
    h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 loadbang;
#X msg 10 40 hello 1 2;
#X obj 10 70 print;
#X connect 0 0 1 0;
#X connect 1 0 2 0;
)");
    CHECK(h.take() == std::vector<std::string>{"print: hello 1 2"});
}

TEST_CASE("message box semicolons reach receivers", "[engine]") {
    Harness h;
    auto& c = h.open(R"(#N canvas 0 0 400 300 12;
#X msg 10 10 \; foo 42 \; foo bar \, 7;
#X obj 10 70 r foo;
#X obj 10 100 print got;
#X connect 1 0 2 0;
)");
    h.run();
    h.rt.engine().sendUiEvent(c.boxes()[0]->object->id(), Symbol("click"), {});
    CHECK(h.take() == std::vector<std::string>{"got: 42", "got: bar", "got: 7"});
}

TEST_CASE("trigger fires right to left and converts", "[engine]") {
    Harness h;
    h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 loadbang;
#X msg 10 30 5;
#X obj 10 50 t b f l;
#X obj 10 90 print A;
#X obj 60 90 print B;
#X obj 110 90 print C;
#X connect 0 0 1 0;
#X connect 1 0 2 0;
#X connect 2 0 3 0;
#X connect 2 1 4 0;
#X connect 2 2 5 0;
)");
    CHECK(h.take() == std::vector<std::string>{"C: 5", "B: 5", "A: bang"});
}

TEST_CASE("dollar arguments in message boxes", "[engine]") {
    Harness h;
    auto& c = h.open(R"(#N canvas 0 0 400 300 12;
#X msg 10 30 value \$2 \$1;
#X obj 10 70 print;
#X connect 0 0 1 0;
)");
    h.run();
    h.send(c.boxes()[0].get(), 0, "list", {Atom(1.0), Atom(Symbol("x"))});
    CHECK(h.take() == std::vector<std::string>{"print: value x 1"});
}

TEST_CASE("route, select, pack and unpack", "[engine]") {
    Harness h;
    auto& c = h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 route foo list;
#X obj 10 50 print foo;
#X obj 60 50 print three;
#X obj 110 50 print rej;
#X obj 10 100 pack 0 s;
#X obj 10 130 print pk;
#X obj 10 160 unpack f f f;
#X obj 10 190 print u0;
#X obj 60 190 print u2;
#X connect 0 0 1 0;
#X connect 0 1 2 0;
#X connect 0 2 3 0;
#X connect 4 0 5 0;
#X connect 6 0 7 0;
#X connect 6 2 8 0;
)");
    h.run();
    auto* route = c.boxes()[0].get();
    h.send(route, 0, "foo", {Atom(1.0), Atom(2.0)});
    h.send(route, 0, "list", {Atom(3.0), Atom(Symbol("x"))});
    h.send(route, 0, "bar");
    auto* pack = c.boxes()[4].get();
    h.send(pack, 1, "symbol", {Atom(Symbol("hi"))});
    h.send(pack, 0, "float", {Atom(9.0)});
    h.send(c.boxes()[6].get(), 0, "list", {Atom(1.0), Atom(2.0), Atom(3.0)});
    CHECK(h.take() == std::vector<std::string>{"foo: 1 2", "three: 3 x", "rej: bar", "pk: 9 hi", "u2: 3",
                                               "u0: 1"});
}

TEST_CASE("subpatch inlets and outlets are compiled away", "[engine]") {
    Harness h;
    auto& c = h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 loadbang;
#N canvas 0 0 300 200 inner 0;
#X obj 10 10 inlet;
#X obj 10 40 list prepend in;
#X obj 10 70 outlet;
#X connect 0 0 1 0;
#X connect 1 0 2 0;
#X restore 10 40 pd inner;
#X obj 10 70 print;
#X connect 0 0 1 0;
#X connect 1 0 2 0;
)");
    auto* sub = c.boxes()[1].get();
    REQUIRE(sub->isSubpatch());
    CHECK(sub->numInlets() == 1);
    CHECK(sub->numOutlets() == 1);
    CHECK(h.take() == std::vector<std::string>{"print: list in"});

    // The plan routes loadbang directly into [list prepend].
    const Plan* plan = h.rt.engine().latestPlan();
    REQUIRE(plan);
    const auto* lb = c.boxes()[0]->object.get();
    const auto it = std::ranges::find_if(plan->bindings, [&](const auto& b) { return b.object == lb; });
    REQUIRE(it != plan->bindings.end());
    const auto& table = plan->outletTables[it->firstOutlet];
    REQUIRE(table.count == 1);
    CHECK(table.begin[0].object->className().str() == "list");
}

TEST_CASE("audio input is wanted only while a patch has adc~", "[engine]") {
    Harness h;
    CHECK_FALSE(h.rt.usesAudioInput());
    auto& sine = h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 osc~ 440;
#X obj 10 40 dac~;
#X connect 0 0 1 0;
)");
    CHECK_FALSE(h.rt.usesAudioInput());
    // Nested in a subpatch counts too.
    auto& mic = h.open(R"(#N canvas 0 0 400 300 12;
#N canvas 0 0 300 200 inner 0;
#X obj 10 10 adc~;
#X restore 10 10 pd inner;
)");
    CHECK(h.rt.usesAudioInput());
    h.rt.closePatch(mic);
    CHECK_FALSE(h.rt.usesAudioInput());
    h.rt.closePatch(sine);
}

TEST_CASE("editing: retext keeps fitting connections", "[engine]") {
    Harness h;
    auto& c = h.rt.newPatch();
    auto& a = c.addBox(BoxKind::Object, 0, 0, parseAtoms("t b b"));
    auto& b = c.addBox(BoxKind::Object, 0, 40, parseAtoms("print x"));
    REQUIRE(c.connect(a, 1, b, 0));
    c.setText(a, parseAtoms("t b"));
    CHECK(c.connections().empty()); // outlet 1 vanished
    REQUIRE(c.connect(a, 0, b, 0));
    c.setText(b, parseAtoms("print y"));
    CHECK(c.connections().size() == 1);
    c.setText(b, parseAtoms("nonexistent-object"));
    CHECK(b.broken());
    CHECK(c.connections().size() == 1); // broken boxes keep their connections
}

TEST_CASE("signal outlets cannot feed control inlets", "[engine]") {
    Harness h;
    auto& c = h.rt.newPatch();
    auto& osc = c.addBox(BoxKind::Object, 0, 0, parseAtoms("osc~ 440"));
    auto& pr = c.addBox(BoxKind::Object, 0, 40, parseAtoms("print"));
    auto& mul = c.addBox(BoxKind::Object, 0, 40, parseAtoms("*~ 0.5"));
    CHECK_FALSE(c.connect(osc, 0, pr, 0));
    CHECK(c.connect(osc, 0, mul, 0));
    CHECK_FALSE(c.connect(osc, 0, mul, 1)); // scalar right inlet
}

TEST_CASE("abstractions get their own $0 and arguments", "[engine]") {
    Harness h;
    const auto dir = std::filesystem::temp_directory_path() / "qure-abs-test";
    std::filesystem::create_directories(dir);
    {
        std::ofstream f(dir / "myabs.pd");
        f << "#N canvas 0 0 300 200 12;\n#X obj 10 10 loadbang;\n#X obj 10 40 list append \\$1 \\$2;\n"
             "#X obj 10 70 list prepend \\$0-x;\n#X obj 10 100 print abs;\n"
             "#X connect 0 0 1 0;\n#X connect 1 0 2 0;\n#X connect 2 0 3 0;\n";
    }
    auto r = h.rt.openPatchText("#N canvas 0 0 400 300 12;\n#X obj 10 10 myabs 7 seven;\n#X obj 10 50 myabs 8 eight;\n",
                                "main.pd", dir);
    REQUIRE(r);
    const auto lines = h.take();
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].ends_with("-x 7 seven"));
    CHECK(lines[1].ends_with("-x 8 eight"));
    CHECK(lines[0] != lines[1].substr(0, lines[0].size())); // different $0
    auto* box = (*r)->boxes()[0].get();
    CHECK(box->child);
    CHECK(box->child->isAbstraction());
    CHECK(writePatch(**r).find("#X obj 10 10 myabs 7 seven;") != std::string::npos);
}

TEST_CASE("plans can be swapped while the engine runs", "[engine][threads]") {
    Harness h;
    auto& c = h.rt.newPatch();
    h.rt.commit();
    std::atomic<bool> stop{false};
    std::thread audio([&] {
        while (!stop.load())
            h.rt.engine().tick();
    });
    h.rt.engine().setDsp(true);
    std::vector<Box*> boxes;
    for (int i = 0; i < 300; ++i) {
        auto& osc = c.addBox(BoxKind::Object, i, 0, parseAtoms("osc~ 220"));
        auto& mul = c.addBox(BoxKind::Object, i, 30, parseAtoms("*~ 0.001"));
        auto& dac = c.addBox(BoxKind::Object, i, 60, parseAtoms("dac~"));
        c.connect(osc, 0, mul, 0);
        c.connect(mul, 0, dac, 0);
        boxes.push_back(&osc);
        h.rt.commit();
        if (i % 3 == 0 && boxes.size() > 2) {
            c.removeBox(*boxes.front());
            boxes.erase(boxes.begin());
            h.rt.commit();
        }
        h.rt.collectGarbage();
    }
    stop = true;
    audio.join();
    h.drain();
    SUCCEED("no crash or sanitizer report while swapping plans");
}

TEST_CASE("abstractions are found as .qd first, then .pd", "[engine]") {
    Harness h;
    const auto dir = std::filesystem::temp_directory_path() / "qure-ext-test";
    std::filesystem::create_directories(dir);
    auto write = [&](const char* file, const char* says) {
        std::ofstream f(dir / file);
        f << "#N canvas 0 0 300 200 12;\n#X obj 10 10 loadbang;\n#X msg 10 40 " << says
          << ";\n#X obj 10 70 print;\n#X connect 0 0 1 0;\n#X connect 1 0 2 0;\n";
    };
    write("both.qd", "from-qd");
    write("both.pd", "from-pd");
    write("onlypd.pd", "pd-only");
    auto r = h.rt.openPatchText("#N canvas 0 0 400 300 12;\n#X obj 10 10 both;\n#X obj 10 50 onlypd;\n",
                                "main.qd", dir);
    REQUIRE(r);
    const auto lines = h.take();
    CHECK(std::ranges::find(lines, "print: from-qd") != lines.end());
    CHECK(std::ranges::find(lines, "print: pd-only") != lines.end());
    CHECK(std::ranges::find(lines, "print: from-pd") == lines.end());
}
