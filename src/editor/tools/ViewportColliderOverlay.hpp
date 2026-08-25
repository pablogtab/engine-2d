#pragma once

#include "../../ecs/EntityManager.hpp"
#include "../../scene/Camera2D.hpp"
#include "../EditorSelection.hpp"
#include "../../physics/PhysicsDebugFrame.hpp"
#include <imgui.h>

namespace Engine {

class EditorEntityVisibilityFilter;

enum class PhysicsOverlayMode {
    None,
    Selected,
    All
};

// Session-only collider visualization. Selected colliders remain visible like
// Unity gizmos; debug mode additionally reveals every collider in the scene.
class ViewportColliderOverlay {
public:
    void set_mode(PhysicsOverlayMode mode) { m_mode = mode; }
    [[nodiscard]] PhysicsOverlayMode mode() const { return m_mode; }
    void cycle_mode();
    [[nodiscard]] PhysicsDebugOptions& debug_options() { return m_debug_options; }
    [[nodiscard]] const PhysicsDebugOptions& debug_options() const { return m_debug_options; }
    [[nodiscard]] PhysicsDebugOptions effective_debug_options() const;

    void render(
        EntityManager& entity_manager,
        const EditorSelection* selection,
        const Camera2D& camera,
        const ImVec2& image_min,
        const ImVec2& image_size,
        const ImVec2& uv_min,
        const ImVec2& uv_max,
        const EditorEntityVisibilityFilter* visibility_filter = nullptr
    ) const;

private:
    PhysicsOverlayMode m_mode{PhysicsOverlayMode::Selected};
    PhysicsDebugOptions m_debug_options{.colliders = true};
};

} // namespace Engine
