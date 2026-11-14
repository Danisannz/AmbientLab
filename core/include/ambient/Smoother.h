#pragma once
#include <algorithm>
#include <cmath>

namespace ambient
{
/** One-pole parameter smoother (allocation-free, real-time safe).
    Time constant ~= timeMs to reach 63% of a step. */
class Smoother
{
public:
    void prepare(double sampleRate, float timeMs) noexcept
    {
        const double samples = std::max(1.0, sampleRate * timeMs * 0.001);
        coeff = std::exp(-1.0 / samples);
    }

    void reset(float value) noexcept { current = target = value; state = value; }
    void setTarget(float value) noexcept { target = value; }

    float next() noexcept
    {
        // State kept in double: float increments stall below 1 ulp near the target.
        state += (1.0 - coeff) * (double(target) - state);
        if (std::abs(double(target) - state) < 1.0e-6) state = target;
        current = float(state);
        return current;
    }

    bool isSmoothing() const noexcept { return current != target; }
    float getCurrent() const noexcept { return current; }
    float getTarget()  const noexcept { return target; }

private:
    double coeff = 0.0, state = 0.0;
    float current = 0.0f, target = 0.0f;
};
} // namespace ambient
