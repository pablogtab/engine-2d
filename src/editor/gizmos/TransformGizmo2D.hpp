#pragma once

#include "../../ecs/EntityManager.hpp"
#include "../../ecs/components/CoreComponents.hpp"
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

enum class TransformTool { Move, Rotate, Scale };
enum class GizmoSpace { World, Local };

struct GizmoSnapSettings {
    bool enabled{false};
    float position_step{16.0f};
    float rotation_step{15.0f};
    float scale_step{0.10f};
};

class TransformGizmo2D {
public:
    TransformGizmo2D() = default;

    // Renders the active 2D transform tool over the selected entities' centroid.
    bool draw_gizmo(
        EntityManager& em,
        const std::unordered_set<EntityID>& selected_entities,
        const Camera2D& camera,
        const ImVec2& viewport_pos,
        const ImVec2& viewport_size,
        bool is_viewport_hovered,
        class EditorCommandHistory* command_history = nullptr
    );

    [[nodiscard]] bool is_using() const { return m_active_axis != GizmoAxis::None; }
    [[nodiscard]] bool is_hovered() const { return m_hovered_axis != GizmoAxis::None; }
    void set_tool(TransformTool tool) { if (!is_using()) m_tool = tool; }
    [[nodiscard]] TransformTool get_tool() const { return m_tool; }
    void set_space(GizmoSpace space) { if (!is_using()) m_space = space; }
    [[nodiscard]] GizmoSpace get_space() const { return m_space; }
    GizmoSnapSettings& snap_settings() { return m_snap; }

private:
    GizmoAxis m_active_axis{GizmoAxis::None};
    GizmoAxis m_hovered_axis{GizmoAxis::None};
    glm::vec2 m_drag_start_world{0.0f, 0.0f};
    glm::vec2 m_drag_axis_x{1.0f, 0.0f};
    glm::vec2 m_drag_axis_y{0.0f, 1.0f};
    glm::vec2 m_drag_origin{0.0f};
    float m_drag_start_angle{0.0f};
    float m_drag_start_distance{1.0f};
    std::unordered_map<EntityID, TransformComponent> m_start_transforms;
    TransformTool m_tool{TransformTool::Move};
    GizmoSpace m_space{GizmoSpace::Local};
    GizmoSnapSettings m_snap;
};

} // namespace Engine
