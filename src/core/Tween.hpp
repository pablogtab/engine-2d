#pragma once

#include <cmath>
#include <algorithm>

namespace Engine {

enum class EaseType {
    Linear,
    InQuad,
    OutQuad,
    InOutQuad,
    InCubic,
    OutCubic,
    InOutCubic,
    OutBounce,
    OutElastic
};

class Tween {
public:
    static float apply(float t, EaseType type) {
        t = std::clamp(t, 0.0f, 1.0f);
        switch (type) {
            case EaseType::Linear:
                return t;
            case EaseType::InQuad:
                return t * t;
            case EaseType::OutQuad:
                return t * (2.0f - t);
            case EaseType::InOutQuad:
                return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
            case EaseType::InCubic:
                return t * t * t;
            case EaseType::OutCubic: {
                float f = t - 1.0f;
                return f * f * f + 1.0f;
            }
            case EaseType::InOutCubic:
                return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
            case EaseType::OutBounce: {
                if (t < (1.0f / 2.75f)) {
                    return 7.5625f * t * t;
                } else if (t < (2.0f / 2.75f)) {
                    float f = t - (1.5f / 2.75f);
                    return 7.5625f * f * f + 0.75f;
                } else if (t < (2.5f / 2.75f)) {
                    float f = t - (2.25f / 2.75f);
                    return 7.5625f * f * f + 0.9375f;
                } else {
                    float f = t - (2.625f / 2.75f);
                    return 7.5625f * f * f + 0.984375f;
                }
            }
            case EaseType::OutElastic: {
                constexpr float c4 = (2.0f * 3.14159265358979323846f) / 3.0f;
                return (t == 0.0f) ? 0.0f : (t == 1.0f) ? 1.0f : std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
            }
        }
        return t;
    }

    static float lerp(float start, float end, float t, EaseType type = EaseType::Linear) {
        return start + (end - start) * apply(t, type);
    }
};

} // namespace Engine
