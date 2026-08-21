#pragma once

#include "../../ecs/EntityManager.hpp"
#include "../../ecs/components/UIComponents.hpp"
#include "../../scene/Camera2D.hpp"
#include "../tools/UILayoutGizmoMath.hpp"

#include <imgui.h>
#include <unordered_set>

namespace Engine {

class EditorCommandHistory;
class EditorEntityVisibilityFilter;

class UILayoutGizmo {
public:
    bool draw_gizmo(
        EntityManager& entities,
        const std::unordered_set<EntityID>& selected_entities,
        const Camera2D& camera,
        const ImVec2& viewport_position,
        const ImVec2& viewport_size,
        bool is_viewport_hovered,
        bool snap_enabled,
        float snap_step,
        EditorCommandHistory* command_history,
        const EditorEntityVisibilityFilter* visibility_filter = nullptr
    );

    [[nodiscard]] bool is_using() const { return m_drag_mode != DragMode::None; }
    [[nodiscard]] bool is_hovered() const { return m_hovered; }

private:
    enum class DragMode {
        None,
        Move,
        Resize,
        AnchorMinimum,
        AnchorMaximum,
        AnchorBoth,
        Pivot
    };

    void cancel_drag(EntityManager& entities);
    void clear_drag();

    EntityID m_drag_entity{0};
    DragMode m_drag_mode{DragMode::None};
    UIResizeHandle m_resize_handle{UIResizeHandle::BottomRight};
    UILayoutComponent m_initial_layout{};
    UIRect m_initial_rect{};
    UIRect m_initial_parent_rect{};
    glm::vec2 m_drag_start_canvas{0.0f};
    bool m_hovered{false};
};

} // namespace Engine
