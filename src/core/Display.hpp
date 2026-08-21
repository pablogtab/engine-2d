#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Engine {

using DisplayId = std::uint32_t;
inline constexpr DisplayId InvalidDisplayId = 0;

enum class WindowMode {
    Windowed,
    BorderlessFullscreen,
    ExclusiveFullscreen
};

enum class DisplayOrientation {
    Unknown,
    Landscape,
    LandscapeFlipped,
    Portrait,
    PortraitFlipped
};

enum class DisplayApplyError {
    None,
    WrongThread,
    UnavailableInEditor,
    Unsupported,
    InvalidDisplay,
    InvalidMode,
    PlatformFailure,
    RollbackFailure
};

struct DisplayRect {
    int x{0};
    int y{0};
    int width{0};
    int height{0};

    bool operator==(const DisplayRect&) const = default;
};

struct DisplayMode {
    int width{0};
    int height{0};
    float refresh_rate{0.0f};
    float pixel_density{1.0f};

    bool operator==(const DisplayMode&) const = default;
};

struct DisplayInfo {
    DisplayId id{InvalidDisplayId};
    std::string name;
    DisplayRect bounds;
    DisplayRect usable_bounds;
    float content_scale{1.0f};
    DisplayOrientation orientation{DisplayOrientation::Unknown};
    std::vector<DisplayMode> modes;
    bool is_primary{false};

    bool operator==(const DisplayInfo&) const = default;
};

struct DisplayCapabilities {
    bool can_change_resolution{false};
    bool can_change_window_mode{false};
    bool can_change_display{false};
    bool can_change_refresh_rate{false};
    bool can_change_vsync{false};

    bool operator==(const DisplayCapabilities&) const = default;
};

struct DisplaySettings {
    DisplayId display_id{InvalidDisplayId};
    WindowMode window_mode{WindowMode::Windowed};
    int window_width{1280};
    int window_height{720};
    float refresh_rate{0.0f};
    bool vsync{true};

    bool operator==(const DisplaySettings&) const = default;
};

struct SafeAreaInsets {
    float left{0.0f};
    float top{0.0f};
    float right{0.0f};
    float bottom{0.0f};

    bool operator==(const SafeAreaInsets&) const = default;
};

struct DisplayState {
    DisplaySettings settings;
    int logical_width{1280};
    int logical_height{720};
    int pixel_width{1280};
    int pixel_height{720};
    float dpi_scale{1.0f};
    DisplayOrientation orientation{DisplayOrientation::Unknown};
    SafeAreaInsets safe_area;

    bool operator==(const DisplayState&) const = default;
};

struct DisplayApplyResult {
    bool success{false};
    DisplayApplyError error{DisplayApplyError::None};
    std::string message;
    DisplayState state;
};

} // namespace Engine
