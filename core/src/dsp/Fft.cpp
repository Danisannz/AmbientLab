#include "ambient/dsp/Fft.h"
#include <cmath>

namespace ambient::dsp
{
void Fft::prepare(int size)
{
    n = size;
    int bits = 0;
    while ((1 << bits) < n) ++bits;
    bitrev.resize(size_t(n));
    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        bitrev[size_t(i)] = r;
    }
    twiddle.resize(size_t(n / 2));
    for (int i = 0; i < n / 2; ++i)
    {
        const double a = -2.0 * 3.14159265358979323846 * i / n;
        twiddle[size_t(i)] = { float(std::cos(a)), float(std::sin(a)) };
    }
}

void Fft::perform(std::complex<float>* d) const noexcept
{
    for (int i = 0; i < n; ++i)
        if (i < bitrev[size_t(i)]) std::swap(d[i], d[bitrev[size_t(i)]]);

    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len >> 1, step = n / len;
        for (int i = 0; i < n; i += len)
            for (int j = 0; j < half; ++j)
            {
                const auto t = twiddle[size_t(j * step)] * d[i + j + half];
                d[i + j + half] = d[i + j] - t;
                d[i + j] += t;
            }
    }
}
} // namespace ambient::dsp
