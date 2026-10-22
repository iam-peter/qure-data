#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace qure {

/// Single-writer (engine thread) / multi-reader (UI) float buffer protected
/// by a sequence lock. Used by scopes, arrays and meters to expose bulk data
/// to the GUI without locks or allocation on the audio thread. Elements are
/// relaxed atomics, so concurrent reads are well defined; the sequence
/// number tells the reader whether it saw a consistent frame.
class SnapshotBuffer {
public:
    explicit SnapshotBuffer(std::size_t capacity)
        : capacity_(capacity), data_(std::make_unique<std::atomic<float>[]>(capacity)) {}

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    /// Engine thread.
    void write(std::span<const float> samples) noexcept {
        const auto n = samples.size() < capacity_ ? samples.size() : capacity_;
        const auto seq = seq_.load(std::memory_order_relaxed);
        seq_.store(seq + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        for (std::size_t i = 0; i < n; ++i)
            data_[i].store(samples[i], std::memory_order_relaxed);
        size_.store(n, std::memory_order_relaxed);
        seq_.store(seq + 2, std::memory_order_release);
    }

    /// UI thread. Returns false if the frame changed while reading (the
    /// caller simply tries again next frame) or nothing new was written
    /// since `lastSeen`.
    bool read(std::vector<float>& out, std::uint64_t& lastSeen) const {
        const auto s1 = seq_.load(std::memory_order_acquire);
        if ((s1 & 1u) || s1 == lastSeen)
            return false;
        const auto n = size_.load(std::memory_order_relaxed);
        out.resize(n);
        for (std::size_t i = 0; i < n; ++i)
            out[i] = data_[i].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq_.load(std::memory_order_relaxed) != s1)
            return false;
        lastSeen = s1;
        return true;
    }

private:
    const std::size_t capacity_;
    std::unique_ptr<std::atomic<float>[]> data_;
    std::atomic<std::size_t> size_{0};
    std::atomic<std::uint64_t> seq_{0};
};

} // namespace qure
