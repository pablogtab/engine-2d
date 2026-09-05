#include "ProjectSettings.hpp"

#include "FileSystem.hpp"
#include "Log.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace Engine {

const char* lighting_quality_preset_to_string(LightingQualityPreset preset) {
    switch (preset) {
        case LightingQualityPreset::Low: return "low";
        case LightingQualityPreset::Medium: return "medium";
        case LightingQualityPreset::High: return "high";
        case LightingQualityPreset::Ultra: return "ultra";
    }
    return "high";
}

LightingQualityPreset lighting_quality_preset_from_string(const std::string& value) {
    if (value == "low") return LightingQualityPreset::Low;
    if (value == "medium") return LightingQualityPreset::Medium;
    if (value == "ultra") return LightingQualityPreset::Ultra;
    return LightingQualityPreset::High;
}

LightingQualitySettings lighting_quality_settings_for_preset(LightingQualityPreset preset) {
    switch (preset) {
        case LightingQualityPreset::Low: return {preset, 0.50f, 2, 1, 0, false, LightingQualityPreset::Medium};
        case LightingQualityPreset::Medium: return {preset, 0.50f, 4, 2, 4, false, LightingQualityPreset::Medium};
        case LightingQualityPreset::High: return {preset, 0.75f, 8, 2, 8, false, LightingQualityPreset::Medium};
        case LightingQualityPreset::Ultra: return {preset, 1.00f, 16, 3, 16, false, LightingQualityPreset::Medium};
    }
    return lighting_quality_settings_for_preset(LightingQualityPreset::High);
}

const char* orientation_policy_to_string(OrientationPolicy policy) {
    switch (policy) {
        case OrientationPolicy::Landscape: return "landscape";
        case OrientationPolicy::Portrait: return "portrait";
        case OrientationPolicy::LandscapeLeft: return "landscape_left";
        case OrientationPolicy::LandscapeRight: return "landscape_right";
        case OrientationPolicy::PortraitUpright: return "portrait_upright";
        case OrientationPolicy::PortraitUpsideDown: return "portrait_upside_down";
        case OrientationPolicy::Auto: return "auto";
    }
    return "auto";
}

OrientationPolicy orientation_policy_from_string(const std::string& value) {
    if (value == "landscape") return OrientationPolicy::Landscape;
    if (value == "portrait") return OrientationPolicy::Portrait;
    if (value == "landscape_left") return OrientationPolicy::LandscapeLeft;
    if (value == "landscape_right") return OrientationPolicy::LandscapeRight;
    if (value == "portrait_upright") return OrientationPolicy::PortraitUpright;
    if (value == "portrait_upside_down") return OrientationPolicy::PortraitUpsideDown;
    return OrientationPolicy::Auto;
}

const char* orientation_policy_sdl_hint(OrientationPolicy policy) {
    switch (policy) {
        case OrientationPolicy::Landscape:
            return "LandscapeLeft LandscapeRight";
        case OrientationPolicy::Portrait:
            return "Portrait PortraitUpsideDown";
        case OrientationPolicy::LandscapeLeft:
            return "LandscapeLeft";
        case OrientationPolicy::LandscapeRight:
            return "LandscapeRight";
        case OrientationPolicy::PortraitUpright:
            return "Portrait";
        case OrientationPolicy::PortraitUpsideDown:
            return "PortraitUpsideDown";
        case OrientationPolicy::Auto:
            return "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown";
    }
    return "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown";
}

namespace {

const char* presentation_to_string(PresentationMode presentation) {
    switch (presentation) {
        case PresentationMode::Fill: return "fill";
        case PresentationMode::Stretch: return "stretch";
        case PresentationMode::Responsive: return "responsive";
        case PresentationMode::Fit: return "fit";
    }
    return "fit";
}

PresentationMode presentation_from_string(const std::string& value) {
    if (value == "fill") return PresentationMode::Fill;
    if (value == "stretch") return PresentationMode::Stretch;
    if (value == "responsive") return PresentationMode::Responsive;
    return PresentationMode::Fit;
}

const char* window_mode_to_string(WindowMode mode) {
    switch (mode) {
        case WindowMode::Windowed: return "windowed";
        case WindowMode::BorderlessFullscreen: return "borderless";
        case WindowMode::ExclusiveFullscreen: return "exclusive";
    }
    return "windowed";
}

WindowMode window_mode_from_string(const std::string& value) {
    if (value == "borderless") return WindowMode::BorderlessFullscreen;
    if (value == "exclusive") return WindowMode::ExclusiveFullscreen;
    return WindowMode::Windowed;
}

} // namespace

bool ProjectSettings::load(const std::string& filepath) {
    ProjectSettings candidate = *this;
    candidate.m_filepath = filepath;
    try {
        const std::string text = FileSystem::read_text(filepath);
        if (text.empty()) return false;

        const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            ENGINE_LOG_ERROR("[PROJECT SETTINGS] Invalid JSON in '{}'", filepath);
            return false;
        }

        const auto& display = root.contains("display") ? root["display"] : nlohmann::json::object();
        candidate.m_display.reference_width = std::clamp(display.value("reference_width", 1280), 64, 16384);
        candidate.m_display.reference_height = std::clamp(display.value("reference_height", 720), 64, 16384);
        candidate.m_display.presentation = presentation_from_string(display.value("presentation", std::string{"fit"}));
        candidate.m_display.default_window_mode = window_mode_from_string(display.value("default_window_mode", std::string{"windowed"}));
        candidate.m_display.default_window_width = std::clamp(display.value("default_window_width", 1280), 320, 16384);
        candidate.m_display.default_window_height = std::clamp(display.value("default_window_height", 720), 240, 16384);
        candidate.m_display.default_refresh_rate = std::max(display.value("default_refresh_rate", 0.0f), 0.0f);
        candidate.m_display.default_vsync = display.value("default_vsync", true);
        candidate.m_display.orientation_policy = orientation_policy_from_string(
            display.value("orientation_policy", std::string{"auto"})
        );
        candidate.m_display.render_resolution = normalize_render_resolution_settings({
            render_scale_mode_from_string(display.value("render_scale_mode", std::string{"fixed"})),
            display.value("fixed_render_scale", 1.0f),
            display.value("dynamic_min_render_scale", 0.5f),
            display.value("dynamic_max_render_scale", 1.0f),
            display.value("dynamic_target_fps", 60.0f)
        });
        const auto& lighting = root.contains("lighting") ? root["lighting"] : nlohmann::json::object();
        candidate.m_lighting = lighting_quality_settings_for_preset(lighting_quality_preset_from_string(
            lighting.value("preset", std::string{"high"})
        ));
        candidate.m_lighting.shadow_mask_scale = std::clamp(lighting.value("shadow_mask_scale", candidate.m_lighting.shadow_mask_scale), 0.25f, 1.0f);
        candidate.m_lighting.max_shadowed_lights = std::clamp(lighting.value("max_shadowed_lights", candidate.m_lighting.max_shadowed_lights), 0, 16);
        candidate.m_lighting.soft_fast_passes = std::clamp(lighting.value("soft_fast_passes", candidate.m_lighting.soft_fast_passes), 0, 3);
        candidate.m_lighting.contact_hardening_samples = std::clamp(lighting.value("contact_hardening_samples", candidate.m_lighting.contact_hardening_samples), 0, 16);
        candidate.m_lighting.mobile_override = lighting.value("mobile_override", false);
        candidate.m_lighting.mobile_preset = lighting_quality_preset_from_string(lighting.value("mobile_preset", std::string{"medium"}));
        ENGINE_LOG_INFO(
            "[PROJECT SETTINGS] Authoring reference: {}x{} ({})",
            candidate.m_display.reference_width,
            candidate.m_display.reference_height,
            presentation_to_string(candidate.m_display.presentation)
        );
        *this = std::move(candidate);
        return true;
    } catch (const nlohmann::json::exception& error) {
        ENGINE_LOG_ERROR("[PROJECT SETTINGS] Invalid settings schema: {}", error.what());
        return false;
    }
}

bool ProjectSettings::save() const {
    nlohmann::json root;
    root["display"] = {
        {"reference_width", m_display.reference_width},
        {"reference_height", m_display.reference_height},
        {"presentation", presentation_to_string(m_display.presentation)},
        {"default_window_mode", window_mode_to_string(m_display.default_window_mode)},
        {"default_window_width", m_display.default_window_width},
        {"default_window_height", m_display.default_window_height},
        {"default_refresh_rate", m_display.default_refresh_rate},
        {"default_vsync", m_display.default_vsync},
        {"orientation_policy", orientation_policy_to_string(m_display.orientation_policy)},
        {"render_scale_mode", render_scale_mode_to_string(m_display.render_resolution.mode)},
        {"fixed_render_scale", m_display.render_resolution.fixed_scale},
        {"dynamic_min_render_scale", m_display.render_resolution.dynamic_min_scale},
        {"dynamic_max_render_scale", m_display.render_resolution.dynamic_max_scale},
        {"dynamic_target_fps", m_display.render_resolution.dynamic_target_fps}
    };
    root["lighting"] = {
        {"preset", lighting_quality_preset_to_string(m_lighting.preset)},
        {"shadow_mask_scale", m_lighting.shadow_mask_scale},
        {"max_shadowed_lights", m_lighting.max_shadowed_lights},
        {"soft_fast_passes", m_lighting.soft_fast_passes},
        {"contact_hardening_samples", m_lighting.contact_hardening_samples},
        {"mobile_override", m_lighting.mobile_override},
        {"mobile_preset", lighting_quality_preset_to_string(m_lighting.mobile_preset)}
    };
    return FileSystem::write_text_atomically(m_filepath, root.dump(2) + "\n");
}

void ProjectSettings::set_display(const ProjectDisplaySettings& settings) {
    m_display = settings;
    m_display.reference_width = std::clamp(settings.reference_width, 64, 16384);
    m_display.reference_height = std::clamp(settings.reference_height, 64, 16384);
    m_display.default_window_width = std::clamp(settings.default_window_width, 320, 16384);
    m_display.default_window_height = std::clamp(settings.default_window_height, 240, 16384);
    m_display.default_refresh_rate = std::max(settings.default_refresh_rate, 0.0f);
    m_display.render_resolution = normalize_render_resolution_settings(settings.render_resolution);
    save();
}

void ProjectSettings::set_lighting(const LightingQualitySettings& settings) {
    m_lighting = settings;
    m_lighting.shadow_mask_scale = std::clamp(m_lighting.shadow_mask_scale, 0.25f, 1.0f);
    m_lighting.max_shadowed_lights = std::clamp(m_lighting.max_shadowed_lights, 0, 16);
    m_lighting.soft_fast_passes = std::clamp(m_lighting.soft_fast_passes, 0, 3);
    m_lighting.contact_hardening_samples = std::clamp(m_lighting.contact_hardening_samples, 0, 16);
    save();
}

} // namespace Engine
