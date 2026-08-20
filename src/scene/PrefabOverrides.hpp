#pragma once

#include "../ecs/EntityManager.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "ComponentRegistry.hpp"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Engine {

class PrefabOverrides {
public:
    // Returns true if the specific component on the entity differs from the master prefab asset on disk.
    [[nodiscard]] static bool is_component_overridden(
        const EntityManager& em,
        EntityID entity,
        const std::string& component_key,
        const ComponentRegistry* registry = nullptr
    );

    // Returns a list of all component keys (e.g. "sprite", "transform", "rigidbody") that differ from the master prefab asset.
    [[nodiscard]] static std::vector<std::string> get_overridden_components(
        const EntityManager& em,
        EntityID entity,
        const ComponentRegistry* registry = nullptr
    );

    // Helper to get master component JSON for an entity's stable prefab local ID.
    [[nodiscard]] static nlohmann::json get_master_component_json(
        const std::string& prefab_asset_path,
        const std::string& component_key,
        EntityID source_local_id = 1
    );

    // Compares two component JSON objects, handling root transform exceptions (e.g. ignoring root position).
    [[nodiscard]] static bool json_differs(
        const std::string& component_key,
        const nlohmann::json& live_json,
        const nlohmann::json& master_json,
        bool is_root_entity = true
    );
};

} // namespace Engine
