#pragma once

#include "VectorOps.hpp"

#include <algorithm>
#include <cstdint>

namespace EngineScript {

struct Random {
    [[nodiscard]] static constexpr std::uint32_t nextUInt(std::uint32_t seed) {
        return seed * 1664525u + 1013904223u;
    }
    [[nodiscard]] static constexpr float value(std::uint32_t seed) {
        return static_cast<float>(nextUInt(seed) >> 8u) * (1.0f / 16777216.0f);
    }
    [[nodiscard]] static constexpr int rangeInt(std::uint32_t seed, int minimum, int maximum) {
        if (minimum > maximum) std::swap(minimum, maximum);
        if (minimum == maximum) return minimum;
        const std::uint32_t span = static_cast<std::uint32_t>(maximum - minimum);
        return minimum + static_cast<int>(nextUInt(seed) % span);
    }
    [[nodiscard]] static constexpr float rangeFloat(std::uint32_t seed, float minimum, float maximum) {
        if (minimum > maximum) std::swap(minimum, maximum);
        return minimum + (maximum - minimum) * value(seed);
    }
    [[nodiscard]] static glm::vec2 insideUnitCircle(std::uint32_t seed) {
        const float angle = value(seed) * 6.28318530717958647692f;
        const float radius = std::sqrt(value(nextUInt(seed)));
        return VectorOps::fromAngle(angle) * radius;
    }
    [[nodiscard]] static glm::vec2 onUnitCircle(std::uint32_t seed) {
        return VectorOps::fromAngle(value(seed) * 6.28318530717958647692f);
    }
    [[nodiscard]] static constexpr bool chance(std::uint32_t seed, float probability) {
        return probability >= 1.0f || (probability > 0.0f && value(seed) < probability);
    }
};

} // namespace EngineScript
