#include "EntitySubtreeArchive.hpp"
#include "../core/Log.hpp"
#include "../core/FileSystem.hpp"
#include "../core/AssetDatabase.hpp"
#include "../ecs/TransformSystem.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include <queue>
#include <algorithm>
#include <unordered_set>
#include <filesystem>

namespace Engine {

namespace {

std::string normalize_prefab_path(const std::string& path) {
    if (path.empty()) return {};
    return std::filesystem::path(path).lexically_normal().generic_string();
}

} // namespace

std::string EntitySubtreeArchive::capture_to_json(
    const EntityManager& em,
    EntityID root_entity,
    const ComponentSerializationContext& context,
    const ComponentRegistry* registry,
    const std::string& base_prefab_path
) {

    if (!em.is_alive(root_entity)) {
        ENGINE_LOG_ERROR("[ARCHIVE] Cannot capture subtree: root entity #{} is not alive", root_entity);
        return "";
    }

    // Ensure all transforms in the source scene are up to date before capturing
    TransformSystem::update(const_cast<EntityManager&>(em));

    const ComponentRegistry& reg = registry ? *registry : default_component_registry();

    // 1. Gather all descendants in breadth-first order
    std::vector<EntityID> subtree_entities;
    std::queue<EntityID> queue;
    queue.push(root_entity);

    while (!queue.empty()) {
        EntityID current = queue.front();
        queue.pop();
        if (!em.is_alive(current)) continue;
        subtree_entities.push_back(current);

        if (em.has_component<HierarchyComponent>(current)) {
            for (EntityID child : em.get_component<HierarchyComponent>(current).children) {
                if (em.is_alive(child)) {
                    queue.push(child);
                }
            }
        }
    }

    // 2. Build map from live EntityID to local 1-based IDs (root is always 1)
    std::unordered_map<EntityID, EntityID> runtime_to_local;
    EntityID next_local_id = 1;
    for (EntityID runtime_id : subtree_entities) {
        runtime_to_local[runtime_id] = next_local_id++;
    }

    // 3. Populate scratch EntityManager with local IDs and remap runtime references to local IDs
    EntityManager temp_em;
    for (EntityID runtime_id : subtree_entities) {
        EntityID local_id = runtime_to_local[runtime_id];
        temp_em.create_entity_with_id(local_id);
        ComponentSnapshotSet snapshot;
        reg.capture_entity(em, runtime_id, snapshot);
        reg.restore_entity(temp_em, local_id, snapshot);
    }

    for (EntityID runtime_id : subtree_entities) {
        EntityID local_id = runtime_to_local[runtime_id];
        reg.remap_entity_references(temp_em, local_id, runtime_to_local);
    }

    // Ensure root entity in the exported prefab asset does not contain a circular PrefabLinkComponent
    const EntityID root_local_id = runtime_to_local[root_entity];
    if (temp_em.has_component<PrefabLinkComponent>(root_local_id)) {
        temp_em.remove_component<PrefabLinkComponent>(root_local_id);
    }
    for (const auto& [runtime_id, local_id] : runtime_to_local) {
        if (local_id == root_local_id || !temp_em.has_component<PrefabLinkComponent>(local_id)) continue;
        if (!temp_em.get_component<PrefabLinkComponent>(local_id).is_root_instance) {
            temp_em.remove_component<PrefabLinkComponent>(local_id);
        }
    }

    // 4. Serialize each entity from local scratch EntityManager

    nlohmann::json root_json;
    root_json["prefab_version"] = CURRENT_PREFAB_VERSION;
    root_json["root_local_id"] = 1;
    if (!base_prefab_path.empty()) {
        root_json["base_prefab_path"] = base_prefab_path;
    }

    std::string tag_name = "Prefab";
    if (em.has_component<TagComponent>(root_entity)) {
        tag_name = em.get_component<TagComponent>(root_entity).tag;
    }
    root_json["name"] = tag_name;

    nlohmann::json entities_array = nlohmann::json::array();
    std::string serialize_error;

    for (EntityID runtime_id : subtree_entities) {
        nlohmann::json entity_json;
        EntityID local_id = runtime_to_local[runtime_id];
        entity_json["id"] = local_id;

        // Serialize registered components from temp_em with local references
        if (!reg.serialize_entity(temp_em, local_id, entity_json, context, serialize_error)) {
            ENGINE_LOG_ERROR("[ARCHIVE] Failed serializing entity #{} for prefab: {}", runtime_id, serialize_error);
            return "";
        }

        // Hierarchy connection with local IDs
        EntityID local_parent_id = 0;
        if (runtime_id != root_entity && em.has_component<HierarchyComponent>(runtime_id)) {
            EntityID runtime_parent = em.get_component<HierarchyComponent>(runtime_id).parent;
            auto it = runtime_to_local.find(runtime_parent);
            if (it != runtime_to_local.end()) {
                local_parent_id = it->second;
            }
        }
        entity_json["hierarchy"] = {{"parent", local_parent_id}};

        // If root entity, normalize local position to (0,0,0) while preserving effective rotation and scale
        if (runtime_id == root_entity && entity_json.contains("transform")) {
            const auto& world_tr = em.get_component<TransformComponent>(root_entity);
            entity_json["transform"]["position"] = {0.0f, 0.0f, 0.0f};
            entity_json["transform"]["rotation"] = world_tr.world_rotation;
            entity_json["transform"]["scale"] = {world_tr.world_scale.x, world_tr.world_scale.y};
        }

        entities_array.push_back(std::move(entity_json));
    }

    root_json["entities"] = std::move(entities_array);
    return root_json.dump(4);
}

bool EntitySubtreeArchive::has_prefab_nesting_cycle(
    const std::string& parent_prefab_path,
    const std::string& candidate_child_prefab_path
) {
    if (parent_prefab_path.empty() || candidate_child_prefab_path.empty()) return false;
    const std::string normalized_parent = normalize_prefab_path(parent_prefab_path);
    const std::string normalized_candidate = normalize_prefab_path(candidate_child_prefab_path);
    if (normalized_parent == normalized_candidate) return true;

    std::unordered_set<std::string> visited;
    std::queue<std::string> queue;
    queue.push(normalized_candidate);
    visited.insert(normalized_candidate);

    while (!queue.empty()) {
        std::string current_path = queue.front();
        queue.pop();

        if (current_path == normalized_parent) return true;

        std::string json_text = FileSystem::read_text(current_path);
        if (!json_text.empty()) {
            try {
                nlohmann::json root = nlohmann::json::parse(json_text);
                // Check the variant base through its stable asset identity.
                if (root.contains("base_asset_handle") && root["base_asset_handle"].is_string()) {
                    const auto handle = AssetHandle::from_string(root["base_asset_handle"].get<std::string>());
                    const AssetRecord* record = handle ? AssetDatabase::get().find(*handle) : nullptr;
                    std::string base = record && record->status == AssetStatus::Ready && record->type == AssetType::Prefab
                        ? normalize_prefab_path(record->source_path) : std::string{};
                    if (base == normalized_parent) return true;
                    if (!base.empty() && visited.insert(base).second) {
                        queue.push(base);
                    }
                }

                // Check nested prefab links in entities
                if (root.contains("entities") && root["entities"].is_array()) {
                    for (const auto& ent : root["entities"]) {
                        if (ent.is_object() && ent.contains("prefab_link") && ent["prefab_link"].is_object()) {
                            std::string link_path;
                            const auto handle = AssetHandle::from_string(ent["prefab_link"].value("asset_handle", ""));
                            if (const AssetRecord* record = handle ? AssetDatabase::get().find(*handle) : nullptr;
                                record && record->status == AssetStatus::Ready && record->type == AssetType::Prefab) {
                                link_path = normalize_prefab_path(record->source_path);
                            }
                            if (link_path == normalized_parent) return true;
                            if (!link_path.empty() && visited.insert(link_path).second) {
                                queue.push(link_path);
                            }
                        }
                    }
                }
            } catch (...) {
                // Ignore parse errors on malformed candidate files
            }
        }
    }
    return false;
}



SubtreeInstantiationResult EntitySubtreeArchive::instantiate_from_json(
    EntityManager& em,
    const std::string& json_text,
    const std::optional<TransformComponent>& desired_world_transform,
    EntityID target_parent,
    const ComponentSerializationContext& context,
    const ComponentRegistry* registry
) {
    if (json_text.empty()) {
        return {.success = false, .error_message = "Prefab JSON content is empty"};
    }

    ComponentSerializationContext effective_context = context;
    if (!effective_context.asset_database) {
        effective_context.asset_database = &AssetDatabase::get();
    }

    // 0. Preflight validation of target parent
    if (target_parent != 0) {
        if (!em.is_alive(target_parent)) {
            return {.success = false, .error_message = "Target parent entity #" + std::to_string(target_parent) + " is not alive"};
        }
        if (!em.has_component<TransformComponent>(target_parent)) {
            return {.success = false, .error_message = "Target parent entity #" + std::to_string(target_parent) + " is missing TransformComponent"};
        }
    }

    // 1. JSON parsing & structural validation
    nlohmann::json root_json;
    try {
        root_json = nlohmann::json::parse(json_text);
    } catch (const std::exception& e) {
        return {.success = false, .error_message = std::string("JSON parse error: ") + e.what()};
    }

    if (!root_json.is_object()) {
        return {.success = false, .error_message = "Invalid prefab JSON: root is not an object"};
    }

    if (!root_json.contains("prefab_version") || !root_json["prefab_version"].is_number_unsigned()) {
        return {.success = false, .error_message = "Missing or invalid 'prefab_version'"};
    }
    uint32_t version = root_json["prefab_version"].get<uint32_t>();
    if (version == 0 || version > CURRENT_PREFAB_VERSION) {
        return {.success = false, .error_message = "Unsupported prefab_version: " + std::to_string(version)};
    }

    if (!root_json.contains("entities") || !root_json["entities"].is_array()) {
        return {.success = false, .error_message = "Invalid prefab JSON structure (missing 'entities' array)"};
    }

    const auto& entities_array = root_json["entities"];
    if (entities_array.empty()) {
        return {.success = false, .error_message = "Prefab contains no entities"};
    }

    if (!root_json.contains("root_local_id") || !root_json["root_local_id"].is_number_unsigned()) {
        return {.success = false, .error_message = "Missing or invalid 'root_local_id'"};
    }
    EntityID root_local_id = root_json["root_local_id"].get<EntityID>();

    // 2. Preflight entity IDs and hierarchy connectivity
    std::unordered_set<EntityID> local_id_set;
    std::unordered_map<EntityID, EntityID> local_parent_map;

    for (const auto& entity_json : entities_array) {
        if (!entity_json.is_object() || !entity_json.contains("id") || !entity_json["id"].is_number_unsigned()) {
            return {.success = false, .error_message = "Entity entry missing or invalid 'id'"};
        }
        EntityID local_id = entity_json["id"].get<EntityID>();
        if (local_id == 0) {
            return {.success = false, .error_message = "Local entity ID cannot be 0"};
        }
        if (!local_id_set.insert(local_id).second) {
            return {.success = false, .error_message = "Duplicate local entity ID: " + std::to_string(local_id)};
        }

        EntityID parent_id = 0;
        if (entity_json.contains("hierarchy") && entity_json["hierarchy"].is_object() && entity_json["hierarchy"].contains("parent")) {
            if (entity_json["hierarchy"]["parent"].is_number_unsigned()) {
                parent_id = entity_json["hierarchy"]["parent"].get<EntityID>();
            }
        }
        local_parent_map[local_id] = parent_id;
    }

    if (local_id_set.find(root_local_id) == local_id_set.end()) {
        return {.success = false, .error_message = "root_local_id (" + std::to_string(root_local_id) + ") not found in entity list"};
    }

    for (const auto& [local_id, parent_id] : local_parent_map) {
        if (local_id == root_local_id) {
            if (parent_id != 0) {
                return {.success = false, .error_message = "Root entity cannot have a local parent"};
            }
        } else {
            if (parent_id == 0 || local_id_set.find(parent_id) == local_id_set.end()) {
                return {.success = false, .error_message = "Entity #" + std::to_string(local_id) + " references invalid local parent #" + std::to_string(parent_id)};
            }
        }
    }

    // Reachability / cycle check from root_local_id
    for (EntityID local_id : local_id_set) {
        EntityID curr = local_id;
        std::unordered_set<EntityID> visited;
        while (curr != 0 && curr != root_local_id) {
            if (!visited.insert(curr).second) {
                return {.success = false, .error_message = "Cycle detected in prefab hierarchy at local entity #" + std::to_string(curr)};
            }
            curr = local_parent_map[curr];
        }
        if (curr != root_local_id && local_id != root_local_id) {
            return {.success = false, .error_message = "Orphan entity #" + std::to_string(local_id) + " is not connected to root"};
        }
    }

    const ComponentRegistry& reg = registry ? *registry : default_component_registry();

    // Stage 1: Create live entities and map local IDs to runtime IDs
    std::vector<EntityID> created_entities;
    std::unordered_map<EntityID, EntityID> local_to_runtime;
    auto rollback = [&]() {
        for (EntityID id : created_entities) {
            if (em.is_alive(id)) em.destroy_entity(id);
        }
        TransformSystem::update(em);
    };

    std::string error_msg;
    for (const auto& entity_json : entities_array) {
        EntityID local_id = entity_json["id"].get<EntityID>();
        EntityID new_id = em.create_entity();
        created_entities.push_back(new_id);
        local_to_runtime[local_id] = new_id;

        if (!reg.deserialize_entity(em, new_id, entity_json, effective_context, error_msg)) {
            rollback();
            return {.success = false, .error_message = "Failed deserializing component: " + error_msg};
        }
    }

    EntityID runtime_root = local_to_runtime[root_local_id];

    // Stage 2: Establish hierarchy and apply transforms
    for (const auto& entity_json : entities_array) {
        EntityID local_id = entity_json["id"].get<EntityID>();
        EntityID runtime_id = local_to_runtime[local_id];

        if (local_id == root_local_id) {
            // Root entity placement
            if (target_parent != 0) {
                // Ensure target parent world transform is up-to-date
                TransformSystem::update(em);

                auto res = em.set_parent(runtime_root, target_parent);
                if (!res.succeeded()) {
                    rollback();
                    return {.success = false, .error_message = "Failed parenting prefab root to target parent: " + std::string(res.message())};
                }

                const TransformComponent* parent_world = &em.get_component<TransformComponent>(target_parent);

                if (desired_world_transform.has_value()) {
                    auto local_opt = TransformSystem::world_to_local_transform(*desired_world_transform, parent_world);
                    if (local_opt && em.has_component<TransformComponent>(runtime_root)) {
                        em.get_component<TransformComponent>(runtime_root) = *local_opt;
                    }
                } else if (em.has_component<TransformComponent>(runtime_root)) {
                    // Place at local (0, 0, 0) under parent
                    em.get_component<TransformComponent>(runtime_root).position = glm::vec3(0.0f, 0.0f, 0.0f);
                }
            } else {
                // Instantiating at root level
                if (desired_world_transform.has_value() && em.has_component<TransformComponent>(runtime_root)) {
                    em.get_component<TransformComponent>(runtime_root) = *desired_world_transform;
                }
            }
        } else {
            // Child entity placement
            EntityID local_parent = local_parent_map[local_id];
            if (local_parent != 0) {
                EntityID runtime_parent = local_to_runtime[local_parent];
                auto res = em.set_parent(runtime_id, runtime_parent);
                if (!res.succeeded()) {
                    rollback();
                    return {.success = false, .error_message = "Failed establishing child hierarchy: " + std::string(res.message())};
                }
            }
        }
    }

    // Stage 3: Remap entity references (e.g. CameraComponent::follow_target)
    for (EntityID runtime_id : created_entities) {
        reg.remap_entity_references(em, runtime_id, local_to_runtime);
    }

    // Stage 4: Finalize components across all created entities
    ComponentFinalizeContext finalize_context{
        .serialization = effective_context,
        .reason = ComponentFinalizeReason::DeserializationCommit
    };

    if (!reg.finalize_entities(em, created_entities, finalize_context, error_msg)) {
        rollback();
        return {.success = false, .error_message = "Failed finalizing prefab entities: " + error_msg};
    }

    TransformSystem::update(em);

    std::string base_prefab_path;
    if (root_json.contains("base_prefab_path") && root_json["base_prefab_path"].is_string()) {
        base_prefab_path = root_json["base_prefab_path"].get<std::string>();
    }

    return {
        .success = true,
        .root_entity = runtime_root,
        .created_entities = std::move(created_entities),
        .local_to_runtime = std::move(local_to_runtime),
        .base_prefab_path = std::move(base_prefab_path),
        .error_message = ""
    };
}


} // namespace Engine
