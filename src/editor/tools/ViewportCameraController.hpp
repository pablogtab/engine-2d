#pragma once

#include "../../scene/GenericScene.hpp"
#include <glm/glm.hpp>
#include <imgui.h>

namespace Engine {

class ViewportCameraController {
public:
    ViewportCameraController() = default;

    // Handles Pan (Right/Middle Drag), Zoom (Mouse Wheel), and Focus ('F') navigation
    void handle_input(GenericScene* scene, bool is_viewport_hovered, EntityID focus_target = 0);
};

} // namespace Engine
