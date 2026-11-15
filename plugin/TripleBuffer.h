#pragma once
// Lock-free single-producer / single-consumer triple buffer: the writer (message thread)
// publishes complete snapshots, the reader (audio thread) always gets the latest complete one.
// No allocation, no locks, no torn reads.
#include <array>
#include <atomic>

namespace ambient
{
template <typename T>
class TripleBuffer
{
public:
    /** Writer: fill back(), then publish(). */
    T& back() noexcept { return buffers[size_t(backIdx)]; }
    void publish() noexcept
    {
        // swap back <-> middle, mark middle fresh
        const int prev = middle.exchange(backIdx | kFresh, std::memory_order_acq_rel);
        backIdx = prev & kIndexMask;
    }
    /** Reader: returns true and updates front() if a new snapshot was published. */
    bool update() noexcept
    {
        if ((middle.load(std::memory_order_acquire) & kFresh) == 0) return false;
        const int prev = middle.exchange(frontIdx, std::memory_order_acq_rel);
        frontIdx = prev & kIndexMask;
        return true;
    }
    const T& front() const noexcept { return buffers[size_t(frontIdx)]; }

private:
    static constexpr int kFresh = 4, kIndexMask = 3;
    std::array<T, 3> buffers {};
    int backIdx = 0, frontIdx = 1;
    std::atomic<int> middle { 2 };
};
} // namespace ambient
