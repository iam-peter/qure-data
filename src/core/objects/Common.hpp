#pragma once

// Small helpers shared by the built-in object implementations.

#include "Engine.hpp"
#include "Object.hpp"
#include "ObjectRegistry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>

namespace qure::objects {

inline double argFloat(AtomSpan args, std::size_t i, double fallback = 0.0) {
    return i < args.size() && args[i].isFloat() ? args[i].asFloat() : fallback;
}

inline Symbol argSymbol(AtomSpan args, std::size_t i, Symbol fallback = {}) {
    return i < args.size() && args[i].isSymbol() ? args[i].asSymbol() : fallback;
}

inline bool argIsSymbol(AtomSpan args, std::size_t i) { return i < args.size() && args[i].isSymbol(); }

/// Pd's "empty" / "-" conventions for unset send/receive names.
inline Symbol nameOrNone(Symbol s) {
    if (s.empty() || s == sym::empty() || s.str() == "-")
        return {};
    return s;
}

/// Appends the Pd-style text of atoms to a fixed buffer without
/// allocating (used by print on the engine thread).
class TextBuffer {
public:
    void append(std::string_view s) {
        for (char c : s)
            if (len_ < buf_.size())
                buf_[len_++] = c;
    }
    void append(char c) {
        if (len_ < buf_.size())
            buf_[len_++] = c;
    }
    void appendFloat(double f) {
        char tmp[32];
        auto r = std::format_to_n(tmp, sizeof tmp, "{:g}", f);
        append(std::string_view(tmp, static_cast<std::size_t>(r.out - tmp)));
    }
    void appendAtom(const Atom& a) {
        switch (a.type()) {
        case Atom::Type::Float: appendFloat(a.asFloat()); break;
        case Atom::Type::Symbol:
        case Atom::Type::DollarSymbol: append(a.asSymbol().str()); break;
        case Atom::Type::Semi: append(';'); break;
        case Atom::Type::Comma: append(','); break;
        case Atom::Type::Dollar:
            append('$');
            appendFloat(a.dollarIndex());
            break;
        }
    }
    void appendAtoms(AtomSpan atoms) {
        for (std::size_t i = 0; i < atoms.size(); ++i) {
            if (i)
                append(' ');
            appendAtom(atoms[i]);
        }
    }
    [[nodiscard]] std::string_view view() const { return {buf_.data(), len_}; }

private:
    std::array<char, 1000> buf_{};
    std::size_t len_ = 0;
};

/// Builds a message from a selector and args into an Atom list as Pd's
/// [list] family sees it: `foo 1 2` → [foo 1 2], `list 1 2` → [1 2],
/// `symbol foo` → [foo].
inline void messageAsList(Symbol selector, AtomSpan args, AtomVector& out) {
    out.clear();
    if (selector != sym::list() && selector != sym::float_() && selector != sym::bang() &&
        selector != sym::symbol())
        out.emplace_back(selector);
    out.insert(out.end(), args.begin(), args.end());
}

} // namespace qure::objects
