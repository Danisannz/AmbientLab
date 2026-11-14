#pragma once
// Lock-free stereo capture ring: audio thread writes, UI thread reads.
//
// * Single producer (audio thread), single consumer (UI thread).
// * Storage allocated once in the constructor; write() never allocates or blocks.
// * The writer never waits for the reader. The reader detects if the frames it
//   copied were overwritten during the copy and discards that read.

#include <atomic>
#include <cstdint>
#include <vector>

namespace ambient
{
class StereoCaptureRing
{
public:
    /** capacityFrames is rounded up to a power of two. */
    explicit StereoCaptureRing(int capacityFrames = 1 << 16)
    {
        int cap = 1;
        while (cap < capacityFrames) cap <<= 1;
        capacity = cap;
        mask = static_cast<uint64_t>(cap - 1);
        left.assign(static_cast<size_t>(cap), 0.0f);
        right.assign(static_cast<size_t>(cap), 0.0f);
    }

    int getCapacity() const noexcept { return capacity; }

    /** Audio thread. Real-time safe. numFrames must be <= capacity/4. */
    void write(const float* l, const float* r, int numFrames) noexcept
    {
        uint64_t w = writePos.load(std::memory_order_relaxed);
        for (int i = 0; i < numFrames; ++i, ++w)
        {
            const auto idx = static_cast<size_t>(w & mask);
            left[idx]  = l[i];
            right[idx] = r ? r[i] : l[i];
        }
        writePos.store(w, std::memory_order_release);
    }

    /** Total frames ever written (monotonic). */
    uint64_t getWritePosition() const noexcept { return writePos.load(std::memory_order_acquire); }

    /** UI thread. Copies frames [startFrame, startFrame + n). Returns false if those
        frames are not (or no longer) valid, e.g. overwritten during the copy. */
    bool read(uint64_t startFrame, int n, float* outL, float* outR) const noexcept
    {
        if (n <= 0 || n > capacity / 2) return false; // reads limited to half the ring (see guard below)
        const uint64_t end = startFrame + static_cast<uint64_t>(n);
        uint64_t w = writePos.load(std::memory_order_acquire);
        if (end > w || w - startFrame > static_cast<uint64_t>(capacity)) return false;

        for (int i = 0; i < n; ++i)
        {
            const auto idx = static_cast<size_t>((startFrame + static_cast<uint64_t>(i)) & mask);
            outL[i] = left[idx];
            outR[i] = right[idx];
        }

        // Validate: the writer must not have lapped the start of our window meanwhile.
        // writePos only covers *published* frames; a block may be half-written past it,
        // so require a guard of capacity/4 frames (>= any realistic host block size).
        w = writePos.load(std::memory_order_acquire);
        return w - startFrame + static_cast<uint64_t>(capacity / 4) <= static_cast<uint64_t>(capacity);
    }

    /** Convenience: the most recent n frames. */
    bool readLatest(int n, float* outL, float* outR) const noexcept
    {
        const uint64_t w = getWritePosition();
        if (w < static_cast<uint64_t>(n)) return false;
        return read(w - static_cast<uint64_t>(n), n, outL, outR);
    }

private:
    std::vector<float> left, right;
    int capacity = 0;
    uint64_t mask = 0;
    alignas(64) std::atomic<uint64_t> writePos { 0 };
};
} // namespace ambient
