#pragma once

#include "../renderer/RenderView.hpp"
#include "../renderer/RenderResolution.hpp"
#include "Display.hpp"
#include <string>

namespace Engine {

enum class OrientationPolicy {
    Auto,
    Landscape,
    Portrait,
    LandscapeLeft,
    LandscapeRight,
    PortraitUpright,
    PortraitUpsideDown
};

enum class LightingQualityPreset { Low, Medium, High, Ultra };

struct LightingQualitySettings {
    LightingQualityPreset preset{LightingQualityPreset::High};
    float shadow_mask_scale{0.75f};
    int max_shadowed_lights{8};
    int soft_fast_passes{2};
    int contact_hardening_samples{8};
    bool mobile_override{false};
    LightingQualityPreset mobile_preset{LightingQualityPreset::Medium};

    bool operator==(const LightingQualitySettings&) const = default;
};

[[nodiscard]] const char* lighting_quality_preset_to_string(LightingQualityPreset preset);
[[nodiscard]] LightingQualitySettings lighting_quality_settings_for_preset(LightingQualityPreset preset);
[[nodiscard]] LightingQualityPreset lighting_quality_preset_from_string(const std::string& value);

[[nodiscard]] const char* orientation_policy_to_string(OrientationPolicy policy);
[[nodiscard]] OrientationPolicy orientation_policy_from_string(const std::string& value);
[[nodiscard]] const char* orientation_policy_sdl_hint(OrientationPolicy policy);

struct ProjectDisplaySettings {
    int reference_width{1280};
    int reference_height{720};
    PresentationMode presentation{PresentationMode::Fit};
    WindowMode default_window_mode{WindowMode::Windowed};
    int default_window_width{1280};
    int default_window_height{720};
    float default_refresh_rate{0.0f};
    bool default_vsync{true};
    OrientationPolicy orientation_policy{OrientationPolicy::Auto};
    RenderResolutionSettings render_resolution;

    [[nodiscard]] float aspect_ratio() const {
        return static_cast<float>(reference_width) / static_cast<float>(reference_height);
    }

    bool operator==(const ProjectDisplaySettings&) const = default;
};

class ProjectSettings {
public:
    bool load(const std::string& filepath = "ProjectSettings/Project.json");
    bool save() const;

    [[nodiscard]] const ProjectDisplaySettings& display() const { return m_display; }
    [[nodiscard]] const LightingQualitySettings& lighting() const { return m_lighting; }
    [[nodiscard]] const std::string& filepath() const { return m_filepath; }
    void set_display(const ProjectDisplaySettings& settings);
    void set_lighting(const LightingQualitySettings& settings);

private:
    std::string m_filepath{"ProjectSettings/Project.json"};
    ProjectDisplaySettings m_display;
    LightingQualitySettings m_lighting;
};

} // namespace Engine
