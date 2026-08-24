#pragma once

#include "../EditorSelection.hpp"
#include "../../scene/Scene.hpp"
#include <glm/glm.hpp>
#include <imgui.h>
#include <vector>

namespace Engine {

class Camera2D;

class ViewportCameraController {
public:
    ViewportCameraController() = default;

    // Handles viewport navigation and its focus-scoped framing shortcuts.
    void handle_input(
        Scene* scene,
        Camera2D* camera,
        const EditorSelection& selection,
        bool is_viewport_hovered,
        bool is_viewport_focused,
        const glm::vec2& viewport_mouse_position,
        const glm::vec2& viewport_mouse_delta
    );

};

} // namespace Engine
