#include "Atom.hpp"
#include "PdText.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace qure;

TEST_CASE("parse numbers and symbols", "[atoms]") {
    auto a = parseAtoms("1 -2.5 1e3 +4 foo - 1x .5 inf");
    REQUIRE(a.size() == 9);
    CHECK(a[0].asFloat() == 1);
    CHECK(a[1].asFloat() == -2.5);
    CHECK(a[2].asFloat() == 1000);
    CHECK(a[3].asFloat() == 4);
    CHECK(a[4].asSymbol().str() == "foo");
    CHECK(a[5].isSymbol());
    CHECK(a[6].isSymbol());
    CHECK(a[7].asFloat() == 0.5);
    CHECK(a[8].isSymbol());
}

TEST_CASE("separators and escapes", "[atoms]") {
    auto a = parseAtoms("; pd dsp 1, 2 a\\ b \\; c\\,d");
    REQUIRE(a.size() == 9);
    CHECK(a[0].type() == Atom::Type::Semi);
    CHECK(a[1].asSymbol().str() == "pd");
    CHECK(a[4].type() == Atom::Type::Comma);
    CHECK(a[6].asSymbol().str() == "a b");
    CHECK(a[7].type() == Atom::Type::Semi);
    CHECK(a[8].asSymbol().str() == "c,d");
}

TEST_CASE("dollars", "[atoms]") {
    auto a = parseAtoms("$1 $0-buf foo-$2 \\$3");
    REQUIRE(a.size() == 4);
    CHECK(a[0].type() == Atom::Type::Dollar);
    CHECK(a[0].dollarIndex() == 1);
    CHECK(a[1].type() == Atom::Type::DollarSymbol);
    CHECK(a[2].type() == Atom::Type::DollarSymbol);
    CHECK(a[3].type() == Atom::Type::Dollar);

    const AtomVector args{Atom(5.0), Atom(Symbol("x"))};
    auto e = expandDollars(a, 1003, args);
    CHECK(e[0].asFloat() == 5);
    CHECK(e[1].asSymbol().str() == "1003-buf");
    CHECK(e[2].asSymbol().str() == "foo-x");
    CHECK(e[3].asFloat() == 0); // out of range
}

TEST_CASE("formatting round trips", "[atoms]") {
    const std::string text = "foo 1 2.5 $1 $0-x; pd dsp 1, bar";
    const auto atoms = parseAtoms(text);
    CHECK(formatAtoms(atoms, TextStyle::Display) == "foo 1 2.5 $1 $0-x; pd dsp 1, bar");
    CHECK(formatAtoms(atoms, TextStyle::File) == "foo 1 2.5 \\$1 \\$0-x \\; pd dsp 1 \\, bar");
    CHECK(formatAtoms(atoms, TextStyle::DisplayMultiline) == "foo 1 2.5 $1 $0-x;\npd dsp 1, bar");
    CHECK(parseAtoms(formatAtoms(atoms, TextStyle::File)) == atoms);
}

TEST_CASE("float formatting matches Pd", "[atoms]") {
    CHECK(formatFloat(1) == "1");
    CHECK(formatFloat(0.1) == "0.1");
    CHECK(formatFloat(-3.25) == "-3.25");
    CHECK(formatFloat(1e-7) == "1e-07");
    CHECK(formatFloat(123456789) == "123456789");
}
