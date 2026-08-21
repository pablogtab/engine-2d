#pragma once

#include "../../ecs/EntityManager.hpp"
#include "../../scene/Camera2D.hpp"
#include "../EditorSelection.hpp"
#include <imgui.h>

namespace Engine {

class EditorEntityVisibilityFilter;

// Selected-light and shadow geometry visualization. Editing position remains
// owned by TransformGizmo2D; radius and polygon values are command-backed in
// the Inspector.
class ViewportLightingOverlay {
public:
    void render(
        EntityManager& entities,
        const EditorSelection* selection,
        const Camera2D& camera,
        const ImVec2& image_min,
        const ImVec2& image_size,
        const ImVec2& uv_min,
        const ImVec2& uv_max,
        const EditorEntityVisibilityFilter* visibility_filter = nullptr
    ) const;
};

} // namespace Engine
