#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace Engine::InspectorPropertyPolicies {

template<typename T>
struct NumericPolicy {
    float speed;
    T minimum;
    T maximum;
    const char* format;
    bool bounded;

    [[nodiscard]] constexpr bool is_bounded() const { return bounded; }
    [[nodiscard]] constexpr T sanitize(T value) const {
        return is_bounded() ? std::clamp(value, minimum, maximum) : value;
    }
};

using FloatPolicy = NumericPolicy<float>;
using IntPolicy = NumericPolicy<int>;

[[nodiscard]] constexpr FloatPolicy unbounded_float(float speed, const char* format = "%.3f") {
    return {speed, 0.0f, 0.0f, format, false};
}

[[nodiscard]] constexpr FloatPolicy bounded_float(
    float speed,
    float minimum,
    float maximum,
    const char* format = "%.3f"
) {
    return {speed, minimum, maximum, format, true};
}

[[nodiscard]] constexpr IntPolicy bounded_int(
    float speed,
    int minimum,
    int maximum,
    const char* format = "%d"
) {
    return {speed, minimum, maximum, format, true};
}

namespace Transform {
inline constexpr auto Position = unbounded_float(1.0f);
inline constexpr auto Depth = bounded_float(0.01f, -1000.0f, 1000.0f);
inline constexpr auto Scale = bounded_float(0.05f, 0.01f, 100.0f);
inline constexpr auto Rotation = bounded_float(0.5f, -360.0f, 360.0f, "%.1f deg");
inline constexpr auto Pivot = bounded_float(0.01f, 0.0f, 1.0f, "%.2f");
} // namespace Transform

namespace Camera {
inline constexpr auto Zoom = bounded_float(0.02f, 0.1f, 20.0f, "%.2fx");
inline constexpr auto FollowOffset = unbounded_float(1.0f);
inline constexpr auto FollowSpeed = bounded_float(0.1f, 0.1f, 20.0f, "%.1f");
inline constexpr auto Bounds = unbounded_float(10.0f);
inline constexpr auto RenderLayerMask = bounded_int(
    1.0f, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), "0x%08X"
);
} // namespace Camera

namespace Sprite {
inline constexpr auto Size = bounded_float(1.0f, 1.0f, 2048.0f);
inline constexpr auto Opacity = bounded_float(0.5f, 0.0f, 100.0f, "%.0f%%");
inline constexpr auto SortingOrder = bounded_int(1.0f, -32768, 32767);
inline constexpr auto RenderLayer = bounded_int(1.0f, 0, 31);
inline constexpr auto OutlineThickness = bounded_float(0.1f, 0.0f, 64.0f, "%.1f px");
} // namespace Sprite

namespace RigidBody {
inline constexpr auto Mass = bounded_float(0.1f, 0.01f, 1000.0f);
inline constexpr auto GravityScale = bounded_float(0.1f, -10.0f, 10.0f);
inline constexpr auto Friction = bounded_float(0.01f, 0.0f, 1.0f);
inline constexpr auto Restitution = bounded_float(0.01f, 0.0f, 1.0f);
} // namespace RigidBody

namespace Collider {
inline constexpr auto Size = bounded_float(1.0f, 1.0f, 100000.0f);
inline constexpr auto Radius = bounded_float(1.0f, 1.0f, 50000.0f);
inline constexpr auto Coordinate = unbounded_float(1.0f);
inline constexpr auto CollisionBits = bounded_int(1.0f, 0, 65535, "0x%04X");
} // namespace Collider

namespace Tilemap {
inline constexpr auto TileSize = bounded_float(1.0f, 8.0f, 256.0f, "%.1f px");
inline constexpr auto PixelSize = bounded_int(1.0f, 1, 4096, "%d px");
inline constexpr auto Friction = bounded_float(0.01f, 0.0f, 1.0f, "%.2f");
inline constexpr auto Restitution = bounded_float(0.01f, 0.0f, 1.0f, "%.2f");
inline constexpr auto CollisionBits = bounded_int(1.0f, 0, 65535, "0x%04X");
inline constexpr auto RenderLayer = bounded_int(1.0f, 0, 31);
} // namespace Tilemap

namespace Animation {
inline constexpr auto GridDimension = bounded_int(1.0f, 1, 256);
inline constexpr auto FrameSize = bounded_float(1.0f, 1.0f, 8192.0f, "%.0f px");
inline constexpr auto GridPadding = bounded_float(1.0f, 0.0f, 8192.0f, "%.0f");
inline constexpr auto FramesPerSecond = bounded_float(0.25f, 0.1f, 240.0f, "%.1f FPS");
} // namespace Animation

} // namespace Engine::InspectorPropertyPolicies
