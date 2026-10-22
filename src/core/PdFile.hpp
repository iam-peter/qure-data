#pragma once

#include "Patch.hpp"

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qure {

/// Reads a complete .pd file into an empty canvas (root or abstraction).
std::expected<void, std::string> readPatch(Canvas& target, std::string_view text);

/// Serialises a canvas (and its subpatches) to Pd's file format.
std::string writePatch(const Canvas& canvas);

/// Serialises a set of boxes and the connections among them, in the format
/// Pd uses for its clipboard (records without the `#N canvas` header).
std::string copyBoxes(const Canvas& canvas, std::span<Box* const> boxes);

struct PasteOptions {
    double dx = 0, dy = 0;          ///< offset applied to pasted positions
    std::span<const BoxId> ids;     ///< ids to reuse, in order (undo)
    std::span<const int> indices;   ///< insert positions, in order (undo)
};
/// Inserts boxes from clipboard text (see copyBoxes); returns them in order.
std::expected<std::vector<Box*>, std::string> pasteBoxes(Canvas& canvas, std::string_view text,
                                                         const PasteOptions& options = {});

} // namespace qure
