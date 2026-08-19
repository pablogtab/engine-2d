#pragma once

#include "../../ecs/EntityManager.hpp"
#include "../../scene/Camera2D.hpp"
#include "../EditorSelection.hpp"
#include <imgui.h>

namespace Engine {

// Session-only collider visualization. Selected colliders remain visible like
// Unity gizmos; debug mode additionally reveals every collider in the scene.
class ViewportColliderOverlay {
public:
    void set_show_all(bool show) { m_show_all = show; }
    [[nodiscard]] bool show_all() const { return m_show_all; }
    void toggle_show_all() { m_show_all = !m_show_all; }

    void render(
        EntityManager& entity_manager,
        const EditorSelection* selection,
        const Camera2D& camera,
        const ImVec2& image_min,
        const ImVec2& image_size,
        const ImVec2& uv_min,
        const ImVec2& uv_max
    ) const;

private:
    bool m_show_all{false};
};

} // namespace Engine
