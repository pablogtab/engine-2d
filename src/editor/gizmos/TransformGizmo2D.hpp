#pragma once

#include "../../ecs/EntityManager.hpp"
#include "../../ecs/Components.hpp"
#include "../../scene/Camera2D.hpp"
#include <glm/glm.hpp>
#include <imgui.h>
#include <unordered_set>
#include <unordered_map>

namespace Engine {

enum class GizmoAxis {
    None = 0,
    AxisX,
    AxisY,
    CenterXY
};

class TransformGizmo2D {
public:
    TransformGizmo2D() = default;

    // Renders the 2D transformation gizmo over the centroid of selected entities and handles group translation
    bool draw_gizmo(
        EntityManager& em,
        const std::unordered_set<EntityID>& selected_entities,
        const Camera2D& camera,
        const ImVec2& viewport_pos,
        const ImVec2& viewport_size,
        bool is_viewport_hovered
    );

    [[nodiscard]] bool is_using() const { return m_active_axis != GizmoAxis::None; }
    [[nodiscard]] bool is_hovered() const { return m_hovered_axis != GizmoAxis::None; }

private:
    GizmoAxis m_active_axis{GizmoAxis::None};
    GizmoAxis m_hovered_axis{GizmoAxis::None};
    glm::vec2 m_drag_start_world{0.0f, 0.0f};
    std::unordered_map<EntityID, glm::vec3> m_entity_start_positions;
};

} // namespace Engine
