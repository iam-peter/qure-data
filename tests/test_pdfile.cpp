#include "Harness.hpp"
#include "PdFile.hpp"

using namespace qure;
using namespace qure::test;

namespace {
const char* kPatch = R"(#N canvas 10 20 600 400 12;
#X obj 30 30 osc~ 440, f 12;
#X msg 30 60 \; pd dsp \$1 \, 5;
#X floatatom 30 90 5 0 0 0 - - - 0;
#X text 30 120 a comment \, with commas;
#N canvas 0 50 450 300 sub 0;
#X obj 10 10 inlet;
#X obj 10 40 outlet;
#X connect 0 0 1 0;
#X restore 30 150 pd sub;
#X obj 30 180 tgl 15 0 empty empty empty 17 7 0 10 #fcfcfc #000000 #000000 0 1;
#X declare -path lib;
#X connect 4 0 0 0;
#X coords 0 -1 1 1 200 140 0;
)";
}

TEST_CASE("read a patch with all box kinds", "[pdfile]") {
    Harness h;
    auto& c = h.open(kPatch);
    REQUIRE(c.boxes().size() == 7);
    CHECK(c.props.windowW == 600);
    CHECK(c.boxes()[0]->width == 12);
    CHECK(c.boxes()[1]->kind == BoxKind::Message);
    CHECK(c.boxes()[1]->displayText() == ";\npd dsp $1, 5");
    CHECK(c.boxes()[2]->kind == BoxKind::FloatAtom);
    CHECK(c.boxes()[3]->kind == BoxKind::Comment);
    CHECK(c.boxes()[3]->displayText() == "a comment, with commas");
    CHECK(c.boxes()[4]->isSubpatch());
    CHECK(c.boxes()[4]->child->name() == "sub");
    CHECK(c.boxes()[5]->object);
    CHECK(c.boxes()[6]->kind == BoxKind::Raw);
    CHECK(c.connections().size() == 1);
    CHECK(c.props.hasCoords);
}

TEST_CASE("write reproduces the file", "[pdfile]") {
    Harness h;
    auto& c = h.open(kPatch);
    CHECK(writePatch(c) == kPatch);
}

TEST_CASE("copy and paste keep inner connections", "[pdfile]") {
    Harness h;
    auto& c = h.rt.newPatch();
    auto& a = c.addBox(BoxKind::Object, 10, 10, parseAtoms("f"));
    auto& b = c.addBox(BoxKind::Object, 10, 40, parseAtoms("+ 1"));
    auto& d = c.addBox(BoxKind::Object, 100, 40, parseAtoms("print"));
    c.connect(a, 0, b, 0);
    c.connect(b, 0, a, 1);
    c.connect(b, 0, d, 0);
    std::vector<Box*> sel{&a, &b};
    const auto text = copyBoxes(c, sel);
    auto pasted = pasteBoxes(c, text, {.dx = 20, .dy = 20});
    REQUIRE(pasted);
    REQUIRE(pasted->size() == 2);
    CHECK((*pasted)[0]->x == 30);
    CHECK(c.connections().size() == 5); // 3 original + 2 copied (not the one to print)
}

TEST_CASE("broken objects keep their connections on save", "[pdfile]") {
    Harness h;
    const char* text = R"(#N canvas 0 0 400 300 12;
#X obj 10 10 no-such-thing 1 2;
#X obj 10 50 print;
#X connect 0 3 1 0;
)";
    auto& c = h.open(text);
    CHECK(c.boxes()[0]->broken());
    CHECK(c.boxes()[0]->numOutlets() == 4);
    CHECK(writePatch(c) == text);
}
