#pragma once
// Minimal radix-2 complex FFT (iterative, in-place). All tables are built in
// prepare(); perform() never allocates, so it is safe on the audio thread.

#include <complex>
#include <vector>

namespace ambient::dsp
{
class Fft
{
public:
    /** size must be a power of two. Allocates. */
    void prepare(int size);
    int getSize() const noexcept { return n; }

    /** In-place forward transform of n complex values. */
    void perform(std::complex<float>* data) const noexcept;

private:
    int n = 0;
    std::vector<int> bitrev;
    std::vector<std::complex<float>> twiddle;
};
} // namespace ambient::dsp
