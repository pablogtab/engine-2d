#pragma once

#include "../../scene/Scene.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace Engine {

// Scene-agnostic editor viewport navigation. It depends only on Scene's
// render-camera contract and entity data, never on a concrete scene helper.
class ViewportNavigation {
public:
    [[nodiscard]] static bool frame_entities(
        Scene* scene,
        const std::vector<EntityID>& entities,
        float padding = 0.15f
    );
    [[nodiscard]] static bool frame_all(Scene* scene, float padding = 0.15f);
    [[nodiscard]] static bool pan_world(Scene* scene, const glm::vec2& world_delta);
    [[nodiscard]] static bool pan_pixels(Scene* scene, const glm::vec2& pixel_delta);
    [[nodiscard]] static bool zoom(Scene* scene, float factor);
    [[nodiscard]] static bool zoom_at(Scene* scene, const glm::vec2& viewport_position, float factor);
    [[nodiscard]] static bool reset(Scene* scene);
};

} // namespace Engine
