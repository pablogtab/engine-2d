#pragma once

#include "EntityManager.hpp"

namespace Engine {

class Camera2D;

// Resolves entity cameras and projects their authoring state into the runtime
// camera used by the renderer. Camera entities remain the only source of truth.
class CameraSystem {
public:
    [[nodiscard]] static EntityID find_primary(EntityManager& entity_manager);
    [[nodiscard]] static bool has_camera(EntityManager& entity_manager);
    static void make_primary(EntityManager& entity_manager, EntityID entity);
    [[nodiscard]] static bool apply_to_runtime(
        EntityManager& entity_manager,
        EntityID entity,
        Camera2D& runtime_camera,
        bool apply_position = true
    );
};

} // namespace Engine
