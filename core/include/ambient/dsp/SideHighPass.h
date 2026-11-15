#pragma once
// 4th-order high-pass (two cascaded TPT SVF high-passes, Q 0.7071) used to remove
// side (L-R) energy below a frequency: keeps the bass mono without touching the mid.
#include <cmath>

namespace ambient::dsp
{
struct SideHighPass
{
    float g = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float s[2][2] {};
    void setCutoff(double hz, double sr) noexcept
    {
        g = float(std::tan(3.14159265358979 * std::fmin(hz, 0.45 * sr) / sr));
        a1 = 1.0f / (1.0f + g * (g + kq)); a2 = g * a1; a3 = g * a2;
    }
    void reset() noexcept { s[0][0] = s[0][1] = s[1][0] = s[1][1] = 0.0f; }
    inline float process(float x) noexcept
    {
        for (auto& st : s)
        {
            const float v3 = x - st[1], v1 = a1 * st[0] + a2 * v3, v2 = st[1] + a2 * st[0] + a3 * v3;
            st[0] = 2.0f * v1 - st[0]; st[1] = 2.0f * v2 - st[1];
            x = x - kq * v1 - v2;
        }
        return x;
    }
    void flushDenormals() noexcept { for (auto& st : s) for (auto& v : st) if (std::fabs(v) < 1.0e-20f) v = 0.0f; }
    static constexpr float kq = 1.41421356f; // 1/Q
};
} // namespace ambient::dsp
