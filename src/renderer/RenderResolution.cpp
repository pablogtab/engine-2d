#include "RenderResolution.hpp"

#include <cmath>
#include <string>

namespace Engine {

RenderResolutionSettings normalize_render_resolution_settings(
    const RenderResolutionSettings& settings
) {
    RenderResolutionSettings result = settings;
    result.fixed_scale = std::clamp(result.fixed_scale, 0.5f, 1.0f);
    result.dynamic_min_scale = std::clamp(result.dynamic_min_scale, 0.5f, 1.0f);
    result.dynamic_max_scale = std::clamp(result.dynamic_max_scale, 0.5f, 1.0f);
    if (result.dynamic_min_scale > result.dynamic_max_scale) {
        std::swap(result.dynamic_min_scale, result.dynamic_max_scale);
    }
    result.dynamic_target_fps = std::clamp(result.dynamic_target_fps, 15.0f, 240.0f);
    return result;
}

const char* render_scale_mode_to_string(RenderScaleMode mode) {
    return mode == RenderScaleMode::Dynamic ? "dynamic" : "fixed";
}

RenderScaleMode render_scale_mode_from_string(const std::string& value) {
    return value == "dynamic" ? RenderScaleMode::Dynamic : RenderScaleMode::Fixed;
}

void DynamicResolutionController::configure(const RenderResolutionSettings& settings) {
    const RenderResolutionSettings normalized = normalize_render_resolution_settings(settings);
    if (normalized == m_settings) return;

    const RenderScaleMode previous_mode = m_settings.mode;
    m_settings = normalized;
    if (m_settings.mode == RenderScaleMode::Fixed) {
        m_current_scale = m_settings.fixed_scale;
    } else if (previous_mode != RenderScaleMode::Dynamic) {
        m_current_scale = m_settings.dynamic_max_scale;
        m_smoothed_frame_seconds = 0.0f;
        m_adjustment_cooldown = 0.0f;
    } else {
        m_current_scale = std::clamp(
            m_current_scale,
            m_settings.dynamic_min_scale,
            m_settings.dynamic_max_scale
        );
    }
}

float DynamicResolutionController::update(float frame_seconds) {
    if (m_settings.mode == RenderScaleMode::Fixed) {
        m_current_scale = m_settings.fixed_scale;
        return m_current_scale;
    }

    // Ignore suspended/debugger frames and invalid timings. They are not a
    // useful signal for a sustained resolution decision.
    if (!std::isfinite(frame_seconds) || frame_seconds <= 0.0f || frame_seconds > 0.25f) {
        return m_current_scale;
    }

    constexpr float smoothing = 0.1f;
    m_smoothed_frame_seconds = m_smoothed_frame_seconds <= 0.0f
        ? frame_seconds
        : m_smoothed_frame_seconds + (frame_seconds - m_smoothed_frame_seconds) * smoothing;
    m_adjustment_cooldown = std::max(0.0f, m_adjustment_cooldown - frame_seconds);
    if (m_adjustment_cooldown > 0.0f) return m_current_scale;

    const float target_seconds = 1.0f / m_settings.dynamic_target_fps;
    constexpr float scale_step = 0.05f;
    if (m_smoothed_frame_seconds > target_seconds * 1.08f) {
        m_current_scale = std::max(m_settings.dynamic_min_scale, m_current_scale - scale_step);
        m_adjustment_cooldown = 0.5f;
    } else if (m_smoothed_frame_seconds < target_seconds * 0.85f) {
        m_current_scale = std::min(m_settings.dynamic_max_scale, m_current_scale + scale_step);
        m_adjustment_cooldown = 1.0f;
    }
    return m_current_scale;
}

} // namespace Engine
