#pragma once

#include "Atom.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace qure {

enum class TextStyle {
    Display,          ///< box contents as shown on screen: `1, 2; pd dsp 1`
    DisplayMultiline, ///< like Display, with a line break after each `;`
    File,             ///< inside a .pd record: `1 \, 2 \; pd dsp \$1`
};

/// One lexical token. `separator` is true for an *unescaped* `;` or `,`,
/// which in a .pd file terminates a record (`;`) or introduces a box
/// attribute such as `, f 12` (`,`). Escaped ones are ordinary Semi/Comma
/// atoms belonging to the box contents.
struct Token {
    Atom atom;
    bool separator = false;
};

void tokenize(std::string_view text, const std::function<void(const Token&)>& emit);

std::string formatAtoms(AtomSpan atoms, TextStyle style);

} // namespace qure
