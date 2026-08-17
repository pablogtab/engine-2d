#pragma once

#include "../EditorSelection.hpp"
#include "../../scene/GenericScene.hpp"
#include <glm/glm.hpp>
#include <imgui.h>

namespace Engine {

class ViewportCameraController {
public:
    ViewportCameraController() = default;

    // Handles viewport navigation and its focus-scoped framing shortcuts.
    void handle_input(
        GenericScene* scene,
        const EditorSelection& selection,
        bool is_viewport_hovered,
        bool is_viewport_focused,
        const glm::vec2& viewport_mouse_position
    );
};

} // namespace Engine
