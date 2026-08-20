#pragma once

#include "../ecs/EntityManager.hpp"
#include <string>
#include <vector>

struct SDL_Renderer;

namespace Engine {

class PrefabPropagation {
public:
    // Finds all alive instances in `em` linking to `prefab_asset_path`, replacing their
    // internal subtrees with the fresh prefab definition from disk while preserving each
    // instance's world transform and scene parent.
    // `skip_entity`: Optional entity to exclude (e.g. the source instance that just applied its changes).
    // Returns the list of new root entity IDs created for the updated instances.
    static std::vector<EntityID> propagate_to_instances(
        EntityManager& em,
        const std::string& prefab_asset_path,
        EntityID skip_entity = 0,
        SDL_Renderer* renderer = nullptr
    );
};

} // namespace Engine
