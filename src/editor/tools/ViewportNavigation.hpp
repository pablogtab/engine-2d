#pragma once

#include "../../scene/Scene.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace Engine {

class Camera2D;

// Scene-agnostic editor viewport navigation. It depends only on Scene's
// render-camera contract and entity data, never on a concrete scene helper.
class ViewportNavigation {
public:
    [[nodiscard]] static bool frame_entities(
        Scene* scene,
        Camera2D* camera,
        const std::vector<EntityID>& entities,
        float padding = 0.15f
    );
    [[nodiscard]] static bool frame_all(Scene* scene, Camera2D* camera, float padding = 0.15f);
    [[nodiscard]] static bool pan_world(Camera2D* camera, const glm::vec2& world_delta);
    [[nodiscard]] static bool pan_pixels(Camera2D* camera, const glm::vec2& pixel_delta);
    [[nodiscard]] static bool zoom(Camera2D* camera, float factor);
    [[nodiscard]] static bool zoom_at(Camera2D* camera, const glm::vec2& viewport_position, float factor);
    [[nodiscard]] static bool reset(Camera2D* camera);
};

} // namespace Engine
