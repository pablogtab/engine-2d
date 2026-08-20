#pragma once

#include "../ecs/EntityManager.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "ComponentRegistry.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <glm/vec3.hpp>

namespace Engine {

struct SubtreeInstantiationResult {
    bool success{false};
    EntityID root_entity{0};
    std::vector<EntityID> created_entities;
    std::string base_prefab_path;
    std::string error_message;
};

class EntitySubtreeArchive {
public:
    static constexpr std::uint32_t CURRENT_PREFAB_VERSION = 1;

    // Captures a subtree starting at root_entity, normalizes root transform to (0,0,0) while
    // preserving its effective world rotation/scale and relative child transforms,
    // serializes components via ComponentRegistry, and returns the JSON text.
    [[nodiscard]] static std::string capture_to_json(
        const EntityManager& em,
        EntityID root_entity,
        const ComponentSerializationContext& context,
        const ComponentRegistry* registry = nullptr,
        const std::string& base_prefab_path = ""
    );

    // Checks whether nesting candidate_child_prefab_path into parent_prefab_path would form a cycle.
    [[nodiscard]] static bool has_prefab_nesting_cycle(
        const std::string& parent_prefab_path,
        const std::string& candidate_child_prefab_path
    );

    // Instantiates a subtree archive JSON at desired world transform or under a target parent.
    // Performs full staging and atomic rollback if validation/deserialization/finalization fails.
    [[nodiscard]] static SubtreeInstantiationResult instantiate_from_json(
        EntityManager& em,
        const std::string& json_text,
        const std::optional<TransformComponent>& desired_world_transform,
        EntityID target_parent,
        const ComponentSerializationContext& context,
        const ComponentRegistry* registry = nullptr
    );
};


} // namespace Engine
