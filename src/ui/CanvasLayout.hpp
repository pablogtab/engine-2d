#pragma once

#include "../ecs/components/UIComponents.hpp"
#include "../ecs/EcsTypes.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace Engine {

struct SafeAreaInsets {
    float left{0.0f};
    float top{0.0f};
    float right{0.0f};
    float bottom{0.0f};

    bool operator==(const SafeAreaInsets&) const = default;
};

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
    UIRect safe_rect;
    glm::vec2 scale{1.0f};
};

struct ResolvedUILayout {
    EntityID entity{0};
    UIRect parent_logical_rect;
    UIRect logical_rect;
    UIRect screen_rect;
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
    const Camera2D& editor_camera
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

// Resolves descendants in authored hierarchy order without mutating ECS data.
// Nested canvases own their own layout tree and are not consumed by the parent.
[[nodiscard]] std::vector<ResolvedUILayout> resolve_canvas_hierarchy(
    const EntityManager& entities,
    EntityID canvas_entity,
    const CanvasViewport& viewport,
    const AssetDatabase* asset_database = nullptr
);

} // namespace Engine
