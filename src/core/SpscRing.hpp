#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <span>

namespace qure {

// Fixed rather than std::hardware_destructive_interference_size, whose value
// may differ between translation units compiled with different -mtune.
inline constexpr std::size_t kCacheLine = 64;

/// Wait-free single-producer / single-consumer queue of variable-length
/// records. Used for every message crossing the realtime boundary, so both
/// ends are allocation free.
///
/// Records are stored contiguously; a record that does not fit before the
/// end of the buffer is preceded by a wrap marker and written at offset 0.
class SpscRing {
public:
    explicit SpscRing(std::size_t capacityBytes)
        : capacity_(roundUp(capacityBytes)), buffer_(std::make_unique<std::byte[]>(capacity_)) {}

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    /// Reserves `size` bytes, lets `fill` write them, then publishes.
    /// Returns false (and drops the record) when the queue is full.
    template <class Fill>
    bool push(std::size_t size, Fill&& fill) {
        const std::size_t need = align(size) + kHeader;
        if (need > capacity_ / 2)
            return false;
        const auto write = write_.load(std::memory_order_relaxed);
        const auto read = read_.load(std::memory_order_acquire);
        const std::size_t used = write - read;
        std::size_t pos = write % capacity_;
        std::size_t pad = 0;
        if (pos + need > capacity_)
            pad = capacity_ - pos; // wrap: skip the tail
        if (used + pad + need > capacity_)
            return false;
        if (pad) {
            writeHeader(pos, kWrapMarker);
            pos = 0;
        }
        writeHeader(pos, static_cast<std::uint32_t>(size));
        fill(std::span<std::byte>(buffer_.get() + pos + kHeader, size));
        write_.store(write + pad + need, std::memory_order_release);
        return true;
    }

    /// Calls `visit(std::span<const std::byte>)` for the next record.
    /// Returns false when empty. `visit` may return false to leave the
    /// record in the queue (used to defer commands that need a newer plan).
    template <class Visit>
    bool pop(Visit&& visit) {
        auto read = read_.load(std::memory_order_relaxed);
        const auto write = write_.load(std::memory_order_acquire);
        if (read == write)
            return false;
        std::size_t pos = read % capacity_;
        auto size = readHeader(pos);
        if (size == kWrapMarker) {
            read += capacity_ - pos;
            pos = 0;
            size = readHeader(0);
        }
        if (!visit(std::span<const std::byte>(buffer_.get() + pos + kHeader, size))) {
            // Keep the record, but persist the skip over a wrap marker.
            read_.store(read, std::memory_order_release);
            return false;
        }
        read_.store(read + align(size) + kHeader, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return read_.load(std::memory_order_acquire) == write_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t kHeader = 8;
    static constexpr std::uint32_t kWrapMarker = 0xFFFFFFFFu;

    static std::size_t align(std::size_t n) { return (n + 7) & ~std::size_t{7}; }
    static std::size_t roundUp(std::size_t n) {
        std::size_t c = 1024;
        while (c < n)
            c <<= 1;
        return c;
    }

    void writeHeader(std::size_t pos, std::uint32_t v) { std::memcpy(buffer_.get() + pos, &v, sizeof v); }
    std::uint32_t readHeader(std::size_t pos) const {
        std::uint32_t v;
        std::memcpy(&v, buffer_.get() + pos, sizeof v);
        return v;
    }

    const std::size_t capacity_;
    std::unique_ptr<std::byte[]> buffer_;
    alignas(kCacheLine) std::atomic<std::size_t> write_{0};
    alignas(kCacheLine) std::atomic<std::size_t> read_{0};
};

} // namespace qure
