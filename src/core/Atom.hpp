#pragma once

#include "Symbol.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qure {

/// A message element. Floats are double precision (Pd's are 32-bit), which
/// keeps counters and sample positions exact well past 2^24.
///
/// Besides float and symbol, the "structural" atom types used by message
/// boxes and patch files are represented directly: `;`, `,`, `$n` and
/// symbols containing dollar signs (`$1-foo`). They never travel through
/// connections; message boxes and object creation expand them first.
class Atom {
public:
    enum class Type : unsigned char { Float, Symbol, Semi, Comma, Dollar, DollarSymbol };

    constexpr Atom() noexcept : type_(Type::Float), f_(0.0) {}
    constexpr Atom(double f) noexcept : type_(Type::Float), f_(f) {} // NOLINT(google-explicit-constructor)
    constexpr Atom(int f) noexcept : Atom(static_cast<double>(f)) {} // NOLINT(google-explicit-constructor)
    Atom(Symbol s) noexcept : type_(Type::Symbol), s_(s.data()) {}   // NOLINT(google-explicit-constructor)

    static Atom semi() noexcept { return Atom(Type::Semi); }
    static Atom comma() noexcept { return Atom(Type::Comma); }
    static Atom dollar(int index) noexcept {
        Atom a(Type::Dollar);
        a.f_ = index;
        return a;
    }
    static Atom dollarSymbol(Symbol s) noexcept {
        Atom a(s);
        a.type_ = Type::DollarSymbol;
        return a;
    }

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] bool isFloat() const noexcept { return type_ == Type::Float; }
    [[nodiscard]] bool isSymbol() const noexcept { return type_ == Type::Symbol; }

    /// The float value, or `fallback` when the atom is not a float.
    [[nodiscard]] double asFloat(double fallback = 0.0) const noexcept { return isFloat() ? f_ : fallback; }
    /// The symbol value (also for DollarSymbol), or the empty symbol.
    [[nodiscard]] Symbol asSymbol() const noexcept;
    [[nodiscard]] int dollarIndex() const noexcept { return static_cast<int>(f_); }

    /// Pd's textual rendering: `%g` for floats, escaped symbols.
    [[nodiscard]] std::string toString() const;

    friend bool operator==(const Atom& a, const Atom& b) noexcept;

private:
    explicit Atom(Type t) noexcept : type_(t), f_(0.0) {}

    Type type_;
    union {
        double f_;
        SymbolData* s_;
    };
};

using AtomSpan = std::span<const Atom>;
using AtomVector = std::vector<Atom>;

/// Formats a float the way Pd does (`%g`, six significant digits).
std::string formatFloat(double f);

/// Parses Pd's textual syntax: whitespace separated tokens, `;` and `,` as
/// separators, backslash escapes, `$n` and dollar symbols. Numbers become
/// floats only when the whole token is numeric (so `1e3` is a float but
/// `1x` is a symbol).
AtomVector parseAtoms(std::string_view text);

/// Inverse of parseAtoms. Inserts line breaks after semicolons when
/// `semicolonNewlines` is set (how Pd displays message boxes).
std::string atomsToString(AtomSpan atoms, bool semicolonNewlines = false);

/// Substitutes `$0` with `dollarZero` and `$n` with `args[n-1]` (0 if the
/// argument is missing), returning plain float/symbol atoms. Used when an
/// object box is instantiated inside an abstraction.
AtomVector expandDollars(AtomSpan atoms, int dollarZero, AtomSpan args);

/// Expands a single dollar-symbol such as `$1-foo` or `$0-buf`.
Symbol expandDollarSymbol(Symbol pattern, int dollarZero, AtomSpan args);

} // namespace qure
