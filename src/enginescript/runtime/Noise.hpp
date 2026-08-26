#pragma once

#include <cmath>
#include <cstdint>

namespace EngineScript {

struct Noise {
    [[nodiscard]] static float value1D(std::uint32_t seed, float x) { return value2D(seed, x, 0.0f); }
    [[nodiscard]] static float value2D(std::uint32_t seed, float x, float y) {
        const auto hash = [](std::uint32_t state) { state ^= state >> 16u; state *= 0x7feb352du; state ^= state >> 15u; state *= 0x846ca68bu; return state ^ (state >> 16u); };
        const auto sample = [&](int ix, int iy) { return static_cast<float>(hash(seed ^ static_cast<std::uint32_t>(ix * 73856093) ^ static_cast<std::uint32_t>(iy * 19349663)) >> 8u) / 16777215.0f; };
        const int ix = static_cast<int>(std::floor(x)); const int iy = static_cast<int>(std::floor(y));
        const float tx = x - static_cast<float>(ix); const float ty = y - static_cast<float>(iy);
        const float sx = tx * tx * (3.0f - 2.0f * tx); const float sy = ty * ty * (3.0f - 2.0f * ty);
        const float a = sample(ix, iy) + (sample(ix + 1, iy) - sample(ix, iy)) * sx;
        const float b = sample(ix, iy + 1) + (sample(ix + 1, iy + 1) - sample(ix, iy + 1)) * sx;
        return a + (b - a) * sy;
    }
    [[nodiscard]] static float fbm2D(std::uint32_t seed, float x, float y, int octaves) {
        float total = 0.0f, amplitude = 1.0f, frequency = 1.0f, weight = 0.0f;
        for (int octave = 0; octave < std::clamp(octaves, 1, 8); ++octave) { total += value2D(seed + static_cast<std::uint32_t>(octave), x * frequency, y * frequency) * amplitude; weight += amplitude; amplitude *= 0.5f; frequency *= 2.0f; }
        return total / weight;
    }
};

} // namespace EngineScript
