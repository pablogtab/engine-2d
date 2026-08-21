#pragma once

#include "../../ecs/EntityManager.hpp"
#include "../../ecs/components/UIComponents.hpp"
#include "../../scene/Camera2D.hpp"

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

    [[nodiscard]] bool is_using() const { return m_drag_entity != 0; }
    [[nodiscard]] bool is_hovered() const { return m_hovered; }

private:
    void cancel_drag(EntityManager& entities);
    void clear_drag();

    EntityID m_drag_entity{0};
    UILayoutComponent m_initial_layout{};
    glm::vec2 m_drag_start_screen{0.0f};
    bool m_hovered{false};
};

} // namespace Engine
