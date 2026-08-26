#pragma once

#include <algorithm>
#include <cmath>

namespace EngineScript {

struct Math {
    static constexpr float PI = 3.14159265358979323846f;
    static constexpr float TAU = 6.28318530717958647692f;
    static constexpr float EPSILON = 0.00001f;

    [[nodiscard]] static inline float abs(float value) { return std::fabs(value); }
    [[nodiscard]] static constexpr float sign(float value) { return value > 0.0f ? 1.0f : (value < 0.0f ? -1.0f : 0.0f); }
    [[nodiscard]] static constexpr float min(float a, float b) { return a < b ? a : b; }
    [[nodiscard]] static constexpr float max(float a, float b) { return a > b ? a : b; }
    [[nodiscard]] static constexpr float clamp(float value, float minimum, float maximum) {
        if (minimum > maximum) std::swap(minimum, maximum);
        return std::clamp(value, minimum, maximum);
    }

    [[nodiscard]] static constexpr float saturate(float value) { return clamp(value, 0.0f, 1.0f); }
    [[nodiscard]] static inline float floor(float value) { return std::floor(value); }
    [[nodiscard]] static inline float ceil(float value) { return std::ceil(value); }
    [[nodiscard]] static inline float round(float value) { return std::round(value); }
    [[nodiscard]] static inline float trunc(float value) { return std::trunc(value); }
    [[nodiscard]] static inline float frac(float value) { return value - floor(value); }
    [[nodiscard]] static inline float sqrt(float value) { return std::sqrt(value); }
    [[nodiscard]] static inline float pow(float base, float exponent) { return std::pow(base, exponent); }
    [[nodiscard]] static inline float exp(float value) { return std::exp(value); }
    [[nodiscard]] static inline float log(float value) { return std::log(value); }
    [[nodiscard]] static inline float log10(float value) { return std::log10(value); }
    [[nodiscard]] static inline float sin(float radians) { return std::sin(radians); }
    [[nodiscard]] static inline float cos(float radians) { return std::cos(radians); }
    [[nodiscard]] static inline float tan(float radians) { return std::tan(radians); }
    [[nodiscard]] static inline float asin(float value) { return std::asin(value); }
    [[nodiscard]] static inline float acos(float value) { return std::acos(value); }
    [[nodiscard]] static inline float atan2(float y, float x) { return std::atan2(y, x); }
    [[nodiscard]] static constexpr float lerp(float a, float b, float t) { return a + (b - a) * t; }
    [[nodiscard]] static constexpr float lerpClamped(float a, float b, float t) { return lerp(a, b, saturate(t)); }
    [[nodiscard]] static constexpr float inverseLerp(float a, float b, float value) {
        return a == b ? 0.0f : saturate((value - a) / (b - a));
    }
    [[nodiscard]] static constexpr float remap(float value, float from_min, float from_max, float to_min, float to_max) {
        return from_min == from_max ? to_min : lerp(to_min, to_max, (value - from_min) / (from_max - from_min));
    }
    [[nodiscard]] static constexpr float smoothStep(float a, float b, float t) {
        t = saturate(t);
        return lerp(a, b, t * t * (3.0f - 2.0f * t));
    }
    [[nodiscard]] static inline float repeat(float value, float length) {
        if (length <= 0.0f) return 0.0f;
        float result = std::fmod(value, length);
        return result < 0.0f ? result + length : result;
    }
    [[nodiscard]] static inline float pingPong(float value, float length) {
        if (length <= 0.0f) return 0.0f;
        return length - abs(repeat(value, length * 2.0f) - length);
    }
    [[nodiscard]] static inline bool approximately(float a, float b, float epsilon = EPSILON) {
        return abs(a - b) <= abs(epsilon) * max(1.0f, max(abs(a), abs(b)));
    }
    [[nodiscard]] static constexpr float degreesToRadians(float degrees) { return degrees * (PI / 180.0f); }
    [[nodiscard]] static constexpr float radiansToDegrees(float radians) { return radians * (180.0f / PI); }
};

} // namespace EngineScript
