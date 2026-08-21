#pragma once

#include "../../core/Display.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace Engine {

enum class DevicePreviewOrientation {
    Portrait,
    Landscape
};

struct DevicePreviewProfile {
    std::string_view name;
    int width{390};
    int height{844};
    SafeAreaInsets safe_area{};
};

inline constexpr std::array<DevicePreviewProfile, 3> k_device_preview_profiles{{
    {"Mobile Compact", 390, 844, {0.0f, 47.0f, 0.0f, 34.0f}},
    {"Mobile Tall", 412, 915, {0.0f, 24.0f, 0.0f, 24.0f}},
    {"Tablet", 820, 1180, {0.0f, 24.0f, 0.0f, 20.0f}}
}};

[[nodiscard]] inline DevicePreviewProfile sanitize_device_preview(
    DevicePreviewProfile profile
) {
    profile.width = std::clamp(profile.width, 64, 4096);
    profile.height = std::clamp(profile.height, 64, 4096);
    profile.safe_area.left = std::clamp(
        profile.safe_area.left, 0.0f, static_cast<float>(profile.width)
    );
    profile.safe_area.right = std::clamp(
        profile.safe_area.right,
        0.0f,
        static_cast<float>(profile.width) - profile.safe_area.left
    );
    profile.safe_area.top = std::clamp(
        profile.safe_area.top, 0.0f, static_cast<float>(profile.height)
    );
    profile.safe_area.bottom = std::clamp(
        profile.safe_area.bottom,
        0.0f,
        static_cast<float>(profile.height) - profile.safe_area.top
    );
    return profile;
}

[[nodiscard]] inline DevicePreviewProfile orient_device_preview(
    DevicePreviewProfile profile,
    DevicePreviewOrientation orientation
) {
    profile = sanitize_device_preview(profile);
    const bool is_landscape = profile.width > profile.height;
    const bool wants_landscape = orientation == DevicePreviewOrientation::Landscape;
    if (is_landscape == wants_landscape || profile.width == profile.height) return profile;

    std::swap(profile.width, profile.height);
    profile.safe_area = {
        profile.safe_area.bottom,
        profile.safe_area.left,
        profile.safe_area.top,
        profile.safe_area.right
    };
    return profile;
}

[[nodiscard]] inline SafeAreaInsets scale_device_safe_area(
    const DevicePreviewProfile& profile,
    int output_width,
    int output_height
) {
    const DevicePreviewProfile safe_profile = sanitize_device_preview(profile);
    const float scale_x = static_cast<float>(std::max(output_width, 1)) /
        static_cast<float>(safe_profile.width);
    const float scale_y = static_cast<float>(std::max(output_height, 1)) /
        static_cast<float>(safe_profile.height);
    return {
        safe_profile.safe_area.left * scale_x,
        safe_profile.safe_area.top * scale_y,
        safe_profile.safe_area.right * scale_x,
        safe_profile.safe_area.bottom * scale_y
    };
}

} // namespace Engine
