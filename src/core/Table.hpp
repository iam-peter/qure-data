#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace qure {

/// Implemented by objects that own a named float array ([table],
/// [array define], arrays inside graphs). Other objects find them through
/// the name's Binding::Kind::Table binding.
class TableStorage {
public:
    virtual ~TableStorage() = default;
    /// Engine thread (or control thread before the owner is published).
    [[nodiscard]] virtual std::span<float> tableData() noexcept = 0;
    /// Marks the data as changed so the GUI refreshes its view.
    virtual void tableChanged() noexcept = 0;
    /// Engine thread. Allocates (rare, user initiated).
    virtual void tableResize(std::size_t size) = 0;
    /// Control thread, while loading: initial contents from `#A`.
    virtual void tableLoad(std::size_t offset, std::span<const float> values) = 0;
    /// Control thread, for saving: last snapshot published to the GUI.
    [[nodiscard]] virtual std::vector<float> tableSnapshot() const = 0;
    [[nodiscard]] virtual bool tableSaveContents() const noexcept = 0;
};

} // namespace qure
