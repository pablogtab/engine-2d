#pragma once

#include "../EditorSelection.hpp"
#include "../../scene/Camera2D.hpp"
#include "../../scene/Scene.hpp"
#include <imgui.h>
#include <glm/glm.hpp>

namespace Engine {

class ViewportSelectionTool {
public:
    ViewportSelectionTool() = default;

    // Renders subtle outlines around selected entities
    void render_selection_outlines(
        EntityManager& em,
        const EditorSelection& selection,
        const Camera2D& camera,
        const ImVec2& img_min,
        const ImVec2& img_size
    );

    // Handles Marquee Box Selection (Crossing vs Shift Strict) and Raycast Picking
    void handle_selection_input(
        Scene* active_scene,
        const Camera2D& camera,
        EditorSelection& selection,
        const ImVec2& img_min,
        const ImVec2& img_size,
        bool is_viewport_hovered,
        bool is_gizmo_active
    );

    [[nodiscard]] bool is_box_selecting() const { return m_is_box_selecting; }

private:
    bool m_is_box_selecting{false};
    ImVec2 m_box_select_start{0.0f, 0.0f};
};

} // namespace Engine
