#pragma once

#include "Math.hpp"

namespace EngineScript {

struct Ease {
    [[nodiscard]] static constexpr float linear(float t) { return Math::saturate(t); }
    [[nodiscard]] static constexpr float inQuad(float t) { t = Math::saturate(t); return t * t; }
    [[nodiscard]] static constexpr float outQuad(float t) { t = Math::saturate(t); return 1.0f - (1.0f - t) * (1.0f - t); }
    [[nodiscard]] static constexpr float inOutQuad(float t) {
        t = Math::saturate(t);
        return t < 0.5f ? 2.0f * t * t : 1.0f - ((-2.0f * t + 2.0f) * (-2.0f * t + 2.0f)) / 2.0f;
    }
    [[nodiscard]] static inline float inSine(float t) { return 1.0f - Math::cos(Math::saturate(t) * Math::PI * 0.5f); }
    [[nodiscard]] static inline float outSine(float t) { return Math::sin(Math::saturate(t) * Math::PI * 0.5f); }
    [[nodiscard]] static inline float inOutSine(float t) { return -(Math::cos(Math::saturate(t) * Math::PI) - 1.0f) * 0.5f; }
    [[nodiscard]] static constexpr float inCubic(float t) { t = Math::saturate(t); return t * t * t; }
    [[nodiscard]] static constexpr float outCubic(float t) { t = Math::saturate(t) - 1.0f; return t * t * t + 1.0f; }
    [[nodiscard]] static constexpr float inOutCubic(float t) {
        t = Math::saturate(t);
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - ((-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f)) * 0.5f;
    }
    [[nodiscard]] static inline float inExpo(float t) { t = Math::saturate(t); return t == 0.0f ? 0.0f : Math::pow(2.0f, 10.0f * t - 10.0f); }
    [[nodiscard]] static inline float outExpo(float t) { t = Math::saturate(t); return t == 1.0f ? 1.0f : 1.0f - Math::pow(2.0f, -10.0f * t); }
    [[nodiscard]] static inline float inOutExpo(float t) {
        t = Math::saturate(t);
        if (t == 0.0f || t == 1.0f) return t;
        return t < 0.5f ? Math::pow(2.0f, 20.0f * t - 10.0f) * 0.5f : (2.0f - Math::pow(2.0f, -20.0f * t + 10.0f)) * 0.5f;
    }
    [[nodiscard]] static constexpr float inBack(float t) { t = Math::saturate(t); constexpr float c = 1.70158f; return (c + 1.0f) * t * t * t - c * t * t; }
    [[nodiscard]] static constexpr float outBack(float t) { t = Math::saturate(t) - 1.0f; constexpr float c = 1.70158f; return 1.0f + (c + 1.0f) * t * t * t + c * t * t; }
    [[nodiscard]] static constexpr float inOutBack(float t) {
        t = Math::saturate(t);
        constexpr float c = 1.70158f * 1.525f;
        return t < 0.5f ? ((2.0f * t) * (2.0f * t) * ((c + 1.0f) * 2.0f * t - c)) * 0.5f
                        : (((2.0f * t - 2.0f) * (2.0f * t - 2.0f) * ((c + 1.0f) * (t * 2.0f - 2.0f) + c)) + 2.0f) * 0.5f;
    }
};

} // namespace EngineScript
