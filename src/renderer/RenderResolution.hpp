#pragma once

#include <algorithm>
#include <string>

namespace Engine {

enum class RenderScaleMode {
    Fixed,
    Dynamic
};

struct RenderResolutionSettings {
    RenderScaleMode mode{RenderScaleMode::Fixed};
    float fixed_scale{1.0f};
    float dynamic_min_scale{0.5f};
    float dynamic_max_scale{1.0f};
    float dynamic_target_fps{60.0f};

    bool operator==(const RenderResolutionSettings&) const = default;
};

[[nodiscard]] RenderResolutionSettings normalize_render_resolution_settings(
    const RenderResolutionSettings& settings
);
[[nodiscard]] const char* render_scale_mode_to_string(RenderScaleMode mode);
[[nodiscard]] RenderScaleMode render_scale_mode_from_string(const std::string& value);

class DynamicResolutionController {
public:
    void configure(const RenderResolutionSettings& settings);
    float update(float frame_seconds);

    [[nodiscard]] float current_scale() const { return m_current_scale; }
    [[nodiscard]] float smoothed_fps() const {
        return m_smoothed_frame_seconds > 0.0f ? 1.0f / m_smoothed_frame_seconds : 0.0f;
    }
    [[nodiscard]] const RenderResolutionSettings& settings() const { return m_settings; }

private:
    RenderResolutionSettings m_settings;
    float m_current_scale{1.0f};
    float m_smoothed_frame_seconds{0.0f};
    float m_adjustment_cooldown{0.0f};
};

} // namespace Engine
