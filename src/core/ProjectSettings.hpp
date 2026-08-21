#pragma once

#include "../renderer/RenderView.hpp"
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

    [[nodiscard]] float aspect_ratio() const {
        return static_cast<float>(reference_width) / static_cast<float>(reference_height);
    }

    bool operator==(const ProjectDisplaySettings&) const = default;
};

class ProjectSettings {
public:
    bool load(const std::string& filepath = "config/project.json");
    bool save() const;

    [[nodiscard]] const ProjectDisplaySettings& display() const { return m_display; }
    [[nodiscard]] const std::string& filepath() const { return m_filepath; }
    void set_display(const ProjectDisplaySettings& settings);

private:
    std::string m_filepath{"config/project.json"};
    ProjectDisplaySettings m_display;
};

} // namespace Engine
