#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace ambient
{
/** Wait-free single-producer / single-consumer ring buffer.
    Capacity must be a power of two. Used for audio <-> UI messages and meters. */
template <typename T, std::size_t Capacity>
class SpscFifo
{
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    bool push(const T& v) noexcept
    {
        const auto w = write.load(std::memory_order_relaxed);
        if (w - read.load(std::memory_order_acquire) == Capacity) return false;
        buf[w & (Capacity - 1)] = v;
        write.store(w + 1, std::memory_order_release);
        return true;
    }

    std::optional<T> pop() noexcept
    {
        const auto r = read.load(std::memory_order_relaxed);
        if (r == write.load(std::memory_order_acquire)) return std::nullopt;
        T v = buf[r & (Capacity - 1)];
        read.store(r + 1, std::memory_order_release);
        return v;
    }

    std::size_t size() const noexcept
    {
        return write.load(std::memory_order_acquire) - read.load(std::memory_order_acquire);
    }

private:
    std::array<T, Capacity> buf {};
    alignas(64) std::atomic<std::size_t> write { 0 };
    alignas(64) std::atomic<std::size_t> read  { 0 };
};
} // namespace ambient
