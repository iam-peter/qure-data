// Behaviour of individual built-in objects, compared against Pd.

#include "Harness.hpp"

using namespace qure;
using namespace qure::test;

namespace {

/// Builds [obj] → [print] and returns (harness-owned) boxes.
struct Probe {
    Harness h;
    Canvas* c = nullptr;
    Box* obj = nullptr;

    explicit Probe(std::string_view text, int outlet = 0) {
        c = &h.rt.newPatch();
        obj = &c->addBox(BoxKind::Object, 0, 0, parseAtoms(text));
        REQUIRE_FALSE(obj->broken());
        for (int o = 0; o < obj->numOutlets(); ++o) {
            auto& p = c->addBox(BoxKind::Object, 0, 40, parseAtoms("print " + std::to_string(o)));
            c->connect(*obj, o, p, 0);
        }
        (void)outlet;
        h.rt.commit();
        h.run();
    }
    std::vector<std::string> send(int inlet, std::string_view sel, std::vector<Atom> args = {}) {
        h.send(obj, inlet, sel, std::move(args));
        return h.take();
    }
};

using Lines = std::vector<std::string>;

} // namespace

TEST_CASE("[float] stores and outputs", "[objects]") {
    Probe p("f 3");
    CHECK(p.send(0, "bang") == Lines{"0: 3"});
    CHECK(p.send(1, "float", {Atom(5.0)}).empty());
    CHECK(p.send(0, "bang") == Lines{"0: 5"});
    CHECK(p.send(0, "set", {Atom(7.0)}).empty());
    CHECK(p.send(0, "list", {Atom(1.0), Atom(2.0)}) == Lines{"0: 1"});
    CHECK(p.send(0, "bang") == Lines{"0: 1"});
}

TEST_CASE("[int] truncates toward zero", "[objects]") {
    Probe p("i");
    CHECK(p.send(0, "float", {Atom(-2.7)}) == Lines{"0: -2"});
}

TEST_CASE("[spigot] and [moses]", "[objects]") {
    Probe s("spigot");
    CHECK(s.send(0, "float", {Atom(1.0)}).empty());
    s.send(1, "float", {Atom(1.0)});
    CHECK(s.send(0, "float", {Atom(1.0)}) == Lines{"0: 1"});

    Probe m("moses 10");
    CHECK(m.send(0, "float", {Atom(3.0)}) == Lines{"0: 3"});
    CHECK(m.send(0, "float", {Atom(10.0)}) == Lines{"1: 10"});
}

TEST_CASE("[list] operations", "[objects]") {
    Probe a("list append x y");
    CHECK(a.send(0, "list", {Atom(1.0), Atom(2.0)}) == Lines{"0: 1 2 x y"});
    Probe pre("list prepend 0");
    CHECK(pre.send(0, "float", {Atom(1.0)}) == Lines{"0: 0 1"});
    Probe sp("list split 2");
    CHECK(sp.send(0, "list", {Atom(1.0), Atom(2.0), Atom(3.0)}) == Lines{"1: 3", "0: 1 2"});
    Probe len("list length");
    CHECK(len.send(0, "foo", {Atom(1.0)}) == Lines{"0: 2"});
    Probe trim("list trim");
    CHECK(trim.send(0, "list", {Atom(Symbol("foo")), Atom(1.0)}) == Lines{"0: foo 1"});
}

TEST_CASE("[value] is shared by name", "[objects]") {
    Probe a("v shared-test-value");
    Probe b("v shared-test-value");
    a.send(0, "float", {Atom(42.0)});
    CHECK(b.send(0, "bang") == Lines{"0: 42"});
}

TEST_CASE("[tgl] toggles between 0 and nonzero", "[objects][gui]") {
    Probe t("tgl 15 0 empty empty empty 17 7 0 10 #fcfcfc #000000 #000000 0 5");
    CHECK(t.send(0, "bang") == Lines{"0: 5"});
    CHECK(t.send(0, "bang") == Lines{"0: 0"});
    CHECK(t.send(0, "float", {Atom(2.0)}) == Lines{"0: 2"});
    CHECK(t.send(0, "bang") == Lines{"0: 0"});
    CHECK(t.send(0, "bang") == Lines{"0: 2"});
}

TEST_CASE("frontend-only objects forward both ways", "[objects][gui]") {
    Harness h;
    h.rt.registry().add("test-kbd", [](const ObjectArgs& a) { return makeFrontendObject(a, 1, 2); });
    auto& c = h.rt.newPatch();
    auto& k = c.addBox(BoxKind::Object, 0, 0, parseAtoms("test-kbd"));
    auto& p = c.addBox(BoxKind::Object, 0, 40, parseAtoms("print note"));
    c.connect(k, 1, p, 0);
    h.rt.commit();
    h.run();
    h.rt.engine().sendUiEvent(k.object->id(), Symbol("out"), std::vector<Atom>{Atom(1.0), Atom(60.0), Atom(100.0)});
    CHECK(h.take() == Lines{"note: 60 100"});
    h.send(&k, 0, "list", {Atom(64.0), Atom(0.0)});
    h.run();
    REQUIRE_FALSE(h.notificationArgs.empty());
    const auto& n = h.notificationArgs.back();
    REQUIRE(n.size() == 4);
    CHECK(n[0].asFloat() == 0);                 // inlet
    CHECK(n[1].asSymbol().str() == "list");     // selector
    CHECK(n[2].asFloat() == 64);
}
