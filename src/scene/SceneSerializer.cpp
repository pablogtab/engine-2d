#include "SceneSerializer.hpp"
#include "ComponentRegistry.hpp"
#include "SceneSchema.hpp"
#include "../core/FileSystem.hpp"
#include "../core/Log.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/TransformSystem.hpp"
#include "../tilemap/Tilemap.hpp"
#include "../core/AssetManager.hpp"
#include "../core/AssetDatabase.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Engine {

SceneSerializer::SceneSerializer(
    Scene* scene,
    SDL_Renderer* renderer,
    ComponentRegistry* component_registry
)
    : m_scene(scene), m_renderer(renderer), m_component_registry(component_registry) {}

std::string SceneSerializer::serialize_to_string() {
    if (!m_scene) {
        ENGINE_LOG_ERROR("[SERIALIZER] Cannot serialize: Scene pointer is null");
        return "";
    }

    json root;
    ComponentRegistry& registry = m_component_registry
        ? *m_component_registry
        : default_component_registry();
    root["scene_name"] = m_scene->get_name();
    root["version"] = SceneSchema::CURRENT_VERSION;
    root["component_versions"] = SceneSchema::component_versions(registry);
    if (const AssetHandle profile_handle = m_scene->get_post_process_profile_handle(); profile_handle.is_valid()) {
        root["post_process_profile_handle"] = profile_handle.to_string();
    }

    json entities_array = json::array();
    EntityManager& em = m_scene->get_entity_manager();
    const ComponentSerializationContext component_context{m_renderer, &AssetManager::get(), &AssetDatabase::get()};
    std::string component_error;
    bool component_serialization_failed = false;

    em.for_each<TransformComponent>([&](EntityID id, const TransformComponent& transform) {
        (void)transform;
        if (component_serialization_failed) return;
        if (!em.is_alive(id)) return;

        // Tilemap colliders are derived runtime data and are rebuilt after load.
        if (em.has_component<TilemapGeneratedColliderComponent>(id)) return;

        json entity_json;
        entity_json["id"] = id;

        if (!registry.serialize_entity(
                em, id, entity_json, component_context, component_error
            )) {
            component_serialization_failed = true;
            return;
        }

        // Store only the authoritative parent relation. The children list is
        // derived by EntityManager::set_parent() while loading, which avoids
        // duplicated or stale hierarchy data in scene files and snapshots.
        if (em.has_component<HierarchyComponent>(id)) {
            const auto& hierarchy = em.get_component<HierarchyComponent>(id);
            entity_json["hierarchy"] = {{"parent", hierarchy.parent}};
        }

        entities_array.push_back(entity_json);
    });

    if (component_serialization_failed) {
        ENGINE_LOG_ERROR("[SERIALIZER] {}", component_error);
        return "";
    }

    root["entities"] = entities_array;
    return root.dump(4);
}

bool SceneSerializer::serialize(const std::string& filepath) {
    std::string json_text = serialize_to_string();
    if (json_text.empty()) return false;

    if (!FileSystem::write_text(filepath, json_text)) {
        ENGINE_LOG_ERROR("[SERIALIZER] Failed to write scene to '{}'", filepath);
        return false;
    }

    ENGINE_LOG_INFO("[SERIALIZER] Successfully saved scene '{}' to '{}'", m_scene->get_name(), filepath);
    return true;
}

bool SceneSerializer::deserialize_from_string(const std::string& json_text) {
    if (!m_scene) {
        ENGINE_LOG_ERROR("[SERIALIZER] Cannot deserialize: Scene pointer is null");
        return false;
    }

    if (json_text.empty()) {
        ENGINE_LOG_ERROR("[SERIALIZER] JSON text is empty");
        return false;
    }

    try {
        json root = json::parse(json_text);
        ComponentRegistry& registry = m_component_registry
            ? *m_component_registry
            : default_component_registry();
        std::string schema_error;
        if (!SceneSchema::migrate_to_current(root, registry, schema_error) ||
            !SceneSchema::validate_structure(root, registry, schema_error)) {
            ENGINE_LOG_ERROR("[SERIALIZER] Scene schema rejected: {}", schema_error);
            return false;
        }
        if (m_scene->supports_post_process_profile_assets() && root.contains("post_process_profile_handle")) {
            if (!root["post_process_profile_handle"].is_string()) {
                ENGINE_LOG_ERROR("[SERIALIZER] Scene post_process_profile_handle must be a string");
                return false;
            }
            const auto profile_handle = AssetHandle::from_string(root["post_process_profile_handle"].get<std::string>());
            if (!profile_handle || !m_scene->set_post_process_profile_handle(*profile_handle, AssetDatabase::get())) {
                ENGINE_LOG_ERROR("[SERIALIZER] Scene references an invalid post-process profile asset");
                return false;
            }
        } else if (m_scene->supports_post_process_profile_assets()) {
            static_cast<void>(m_scene->set_post_process_profile_handle({}, AssetDatabase::get()));
        }

        EntityManager candidate;
        const ComponentSerializationContext component_context{m_renderer, &AssetManager::get(), &AssetDatabase::get()};
        std::vector<EntityID> loaded_entities;
        std::vector<std::pair<EntityID, EntityID>> hierarchy_links;

        if (root.contains("entities") && root["entities"].is_array()) {
            for (const auto& entity_json : root["entities"]) {
                const EntityID serialized_id = entity_json.value("id", EntityID{0});
                const EntityID id = serialized_id != 0
                    ? candidate.create_entity_with_id(serialized_id)
                    : candidate.create_entity();
                if (id == 0) {
                    ENGINE_LOG_ERROR("[SERIALIZER] Duplicate or invalid entity ID {}", serialized_id);
                    return false;
                }

                std::string component_error;
                if (!registry.deserialize_entity(
                        candidate, id, entity_json, component_context, component_error
                    )) {
                    ENGINE_LOG_ERROR("[SERIALIZER] Entity {}: {}", id, component_error);
                    return false;
                }
                loaded_entities.push_back(id);

                // Entity references may point forward in the JSON array, so
                // create every entity before resolving any parent relation.
                if (entity_json.contains("hierarchy") && entity_json["hierarchy"].is_object()) {
                    const auto& hierarchy_json = entity_json["hierarchy"];
                    candidate.add_component<HierarchyComponent>(id, HierarchyComponent{});
                    hierarchy_links.emplace_back(id, hierarchy_json.value("parent", EntityID{0}));
                }

            }

            // Rebuild children from the authoritative parent links only after
            // every serialized entity exists. This is also what makes the
            // Play/Stop snapshot restore entities destroyed during simulation
            // without losing their authoring hierarchy.
            for (const auto& [child, parent] : hierarchy_links) {
                if (parent == 0) continue;
                const HierarchyOperationResult result = candidate.set_parent(child, parent);
                if (!result.succeeded()) {
                    ENGINE_LOG_ERROR(
                        "[SERIALIZER] Cannot parent entity #{} to #{}: {}",
                        child,
                        parent,
                        result.message()
                    );
                    return false;
                }
            }

            TransformSystem::update(candidate);
            for (const EntityID entity : loaded_entities) {
                const auto issues = registry.validate_entity(candidate, entity);
                if (!issues.empty()) {
                    ENGINE_LOG_ERROR(
                        "[SERIALIZER] Entity {} component '{}': {}",
                        entity,
                        issues.front().component_key,
                        issues.front().message
                    );
                    return false;
                }
            }

            const ComponentFinalizeContext staging_context{
                .serialization = component_context,
                .reason = ComponentFinalizeReason::DeserializationStaging
            };
            std::string finalize_error;
            if (!registry.finalize_entities(
                    candidate, loaded_entities, staging_context, finalize_error
                )) {
                ENGINE_LOG_ERROR("[SERIALIZER] Component staging failed: {}", finalize_error);
                return false;
            }
            TransformSystem::update(candidate);
        }

        EntityManager& live = m_scene->get_entity_manager();
        m_scene->swap_entity_manager(candidate);
        registry.reconcile_replacement(candidate, live);

        const std::vector<EntityID> committed_entities = live.get_living_entities();
        const ComponentFinalizeContext commit_context{
            .serialization = component_context,
            .reason = ComponentFinalizeReason::DeserializationCommit
        };
        std::string commit_error;
        if (!registry.finalize_entities(live, committed_entities, commit_context, commit_error)) {
            m_scene->swap_entity_manager(candidate);
            registry.reconcile_replacement(candidate, live);
            const std::vector<EntityID> restored_entities = live.get_living_entities();
            std::string rollback_error;
            const bool rollback_finalized =
                registry.finalize_entities(live, restored_entities, commit_context, rollback_error);
            if (!rollback_finalized && rollback_error.empty()) {
                rollback_error = "unknown rollback finalization error";
            }
            ENGINE_LOG_ERROR(
                "[SERIALIZER] Component commit failed: {}{}",
                commit_error,
                rollback_error.empty() ? "" : "; rollback finalization failed: " + rollback_error
            );
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        ENGINE_LOG_ERROR("[SERIALIZER] Exception parsing scene JSON: {}", e.what());
        return false;
    }
}

bool SceneSerializer::deserialize(const std::string& filepath) {
    std::string json_text = FileSystem::read_text(filepath);
    if (json_text.empty()) {
        ENGINE_LOG_ERROR("[SERIALIZER] Scene file not found or empty: '{}'", filepath);
        return false;
    }

    bool success = deserialize_from_string(json_text);
    if (success) {
        ENGINE_LOG_INFO("[SERIALIZER] Successfully loaded scene '{}' from '{}'", m_scene->get_name(), filepath);
    }
    return success;
}

} // namespace Engine
