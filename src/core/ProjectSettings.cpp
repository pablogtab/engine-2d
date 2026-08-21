#include "ProjectSettings.hpp"

#include "FileSystem.hpp"
#include "Log.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace Engine {

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
    m_filepath = filepath;
    const std::string text = FileSystem::read_text(filepath);
    if (text.empty()) return false;

    const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        ENGINE_LOG_ERROR("[PROJECT SETTINGS] Invalid JSON in '{}'", filepath);
        return false;
    }

    const auto& display = root.contains("display") ? root["display"] : nlohmann::json::object();
    m_display.reference_width = std::clamp(display.value("reference_width", 1280), 64, 16384);
    m_display.reference_height = std::clamp(display.value("reference_height", 720), 64, 16384);
    m_display.presentation = presentation_from_string(display.value("presentation", std::string{"fit"}));
    m_display.default_window_mode = window_mode_from_string(display.value("default_window_mode", std::string{"windowed"}));
    m_display.default_window_width = std::clamp(display.value("default_window_width", 1280), 320, 16384);
    m_display.default_window_height = std::clamp(display.value("default_window_height", 720), 240, 16384);
    m_display.default_refresh_rate = std::max(display.value("default_refresh_rate", 0.0f), 0.0f);
    m_display.default_vsync = display.value("default_vsync", true);
    m_display.orientation_policy = orientation_policy_from_string(
        display.value("orientation_policy", std::string{"auto"})
    );
    ENGINE_LOG_INFO(
        "[PROJECT SETTINGS] Authoring reference: {}x{} ({})",
        m_display.reference_width,
        m_display.reference_height,
        presentation_to_string(m_display.presentation)
    );
    return true;
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
        {"orientation_policy", orientation_policy_to_string(m_display.orientation_policy)}
    };
    return FileSystem::write_text(m_filepath, root.dump(2) + "\n");
}

void ProjectSettings::set_display(const ProjectDisplaySettings& settings) {
    m_display = settings;
    m_display.reference_width = std::clamp(settings.reference_width, 64, 16384);
    m_display.reference_height = std::clamp(settings.reference_height, 64, 16384);
    m_display.default_window_width = std::clamp(settings.default_window_width, 320, 16384);
    m_display.default_window_height = std::clamp(settings.default_window_height, 240, 16384);
    m_display.default_refresh_rate = std::max(settings.default_refresh_rate, 0.0f);
    save();
}

} // namespace Engine
