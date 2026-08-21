#pragma once

#include "../core/Display.hpp"
#include "../ecs/components/UIComponents.hpp"
#include "../ecs/EcsTypes.hpp"
#include <array>
#include <glm/glm.hpp>
#include <vector>

namespace Engine {

struct UIRect {
    glm::vec2 position{0.0f};
    glm::vec2 size{0.0f};

    [[nodiscard]] glm::vec2 min() const { return position; }
    [[nodiscard]] glm::vec2 max() const { return position + size; }
    bool operator==(const UIRect&) const = default;
};

struct CanvasViewport {
    UIRect logical_rect;
    UIRect physical_rect;
    UIRect platform_safe_rect;
    UIRect safe_rect;
    UIRect output_rect;
    glm::vec2 scale{1.0f};
};

struct ResolvedUILayout {
    EntityID entity{0};
    UIRect parent_logical_rect;
    UIRect logical_rect;
    UIRect screen_rect;
    glm::vec2 pivot{0.5f, 0.5f};
    glm::vec2 screen_pivot{0.0f};
    glm::vec2 parent_logical_pivot{0.0f};
    glm::vec2 parent_screen_pivot{0.0f};
    float parent_rotation{0.0f};
    float rotation{0.0f};
};

class EntityManager;
class Camera2D;
class AssetDatabase;

[[nodiscard]] CanvasViewport calculate_canvas_viewport(
    const CanvasComponent& canvas,
    const glm::vec2& output_size,
    const SafeAreaInsets& safe_area = {}
);

// Scene View preview for a screen-space Canvas. The authored reference frame
// is centered at the Canvas entity's world position, so editor camera movement
// reveals a stable scene object instead of a camera-pinned HUD.
[[nodiscard]] CanvasViewport calculate_canvas_editor_viewport(
    const CanvasComponent& canvas,
    const glm::vec2& canvas_world_position,
    const Camera2D& editor_camera,
    const SafeAreaInsets& safe_area = {}
);

[[nodiscard]] UIRect resolve_ui_rect(
    const UILayoutComponent& layout,
    const UIRect& parent_rect
);

[[nodiscard]] glm::vec2 canvas_to_screen(
    const glm::vec2& canvas_position,
    const CanvasViewport& viewport
);

[[nodiscard]] glm::vec2 screen_to_canvas(
    const glm::vec2& screen_position,
    const CanvasViewport& viewport
);

[[nodiscard]] UIRect canvas_to_screen(
    const UIRect& canvas_rect,
    const CanvasViewport& viewport
);

[[nodiscard]] glm::vec2 rotate_ui_point(
    const glm::vec2& point,
    const glm::vec2& origin,
    float rotation_degrees
);

[[nodiscard]] std::array<glm::vec2, 4> resolved_ui_corners(
    const ResolvedUILayout& resolved
);

[[nodiscard]] bool point_in_resolved_ui_rect(
    const ResolvedUILayout& resolved,
    const glm::vec2& point
);

// Resolves descendants in authored hierarchy order without mutating ECS data.
// Nested canvases own their own layout tree and are not consumed by the parent.
[[nodiscard]] std::vector<ResolvedUILayout> resolve_canvas_hierarchy(
    const EntityManager& entities,
    EntityID canvas_entity,
    const CanvasViewport& viewport,
    const AssetDatabase* asset_database = nullptr
);

} // namespace Engine
