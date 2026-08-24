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
#include <algorithm>
#include <unordered_set>

using json = nlohmann::json;

namespace Engine {

SceneSerializer::SceneSerializer(
    Scene* scene,
    SDL_Renderer* renderer,
    ComponentRegistry* component_registry,
    SceneEditorMetadata* editor_metadata
)
    : m_scene(scene),
      m_renderer(renderer),
      m_component_registry(component_registry),
      m_editor_metadata(editor_metadata) {}

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
    root["settings"] = {
        {"physics_enabled", m_scene->settings().physics_enabled},
        {"gravity", {m_scene->settings().gravity.x, m_scene->settings().gravity.y}}
    };
    if (const AssetHandle profile_handle = m_scene->get_post_process_profile_handle(); profile_handle.is_valid()) {
        root["post_process_profile_handle"] = profile_handle.to_string();
    }
    root["render_layer_effect_assignments"] = json::array();
    for (const auto& assignment : m_scene->get_render_layer_effect_assignments()) {
        root["render_layer_effect_assignments"].push_back({
            {"profile_handle", assignment.profile_handle.to_string()},
            {"render_layer", assignment.render_layer},
            {"enabled", assignment.enabled}
        });
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
    if (m_editor_metadata) {
        json component_orders = json::array();
        std::vector<EntityID> ordered_entities;
        ordered_entities.reserve(m_editor_metadata->component_orders.size());
        for (const auto& [entity, order] : m_editor_metadata->component_orders) {
            if (!em.is_alive(entity) || order.empty()) continue;
            ordered_entities.push_back(entity);
        }
        std::sort(ordered_entities.begin(), ordered_entities.end());
        for (const EntityID entity : ordered_entities) {
            const auto& order = m_editor_metadata->component_orders.at(entity);
            component_orders.push_back({{"entity", entity}, {"components", order}});
        }
        if (!component_orders.empty()) {
            root["editor_metadata"] = {{"component_orders", std::move(component_orders)}};
        }
    }
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
        if (root.contains("settings")) {
            const auto& settings = root["settings"];
            if (!settings.is_object() ||
                (settings.contains("physics_enabled") && !settings["physics_enabled"].is_boolean()) ||
                (settings.contains("gravity") &&
                    (!settings["gravity"].is_array() || settings["gravity"].size() != 2 ||
                     !settings["gravity"][0].is_number() || !settings["gravity"][1].is_number()))) {
                ENGINE_LOG_ERROR("[SERIALIZER] Scene settings are invalid");
                return false;
            }
            m_scene->settings().physics_enabled = settings.value("physics_enabled", true);
            if (settings.contains("gravity")) {
                m_scene->settings().gravity = {
                    settings["gravity"][0].get<float>(), settings["gravity"][1].get<float>()
                };
            }
        } else {
            m_scene->settings().physics_enabled = true;
            m_scene->settings().gravity = {0.0f, -980.0f};
        }
        if (root.contains("post_process_profile_handle")) {
            if (!root["post_process_profile_handle"].is_string()) {
                ENGINE_LOG_ERROR("[SERIALIZER] Scene post_process_profile_handle must be a string");
                return false;
            }
            const auto profile_handle = AssetHandle::from_string(root["post_process_profile_handle"].get<std::string>());
            if (!profile_handle) {
                ENGINE_LOG_ERROR("[SERIALIZER] Scene references an invalid post-process profile asset");
                return false;
            }
            m_scene->set_post_process_profile_handle(*profile_handle);
        } else {
            m_scene->set_post_process_profile_handle({});
        }
        std::vector<RenderLayerEffectAssignment> assignments;
        if (root.contains("render_layer_effect_assignments")) {
            if (!root["render_layer_effect_assignments"].is_array()) {
                ENGINE_LOG_ERROR("[SERIALIZER] Scene render_layer_effect_assignments must be an array");
                return false;
            }
            for (const auto& value : root["render_layer_effect_assignments"]) {
                if (!value.is_object() || !value.contains("profile_handle") ||
                    !value["profile_handle"].is_string() || !value.contains("render_layer") ||
                    !value["render_layer"].is_number_unsigned() ||
                    (value.contains("enabled") && !value["enabled"].is_boolean())) return false;
                const auto handle = AssetHandle::from_string(value["profile_handle"].get<std::string>());
                const unsigned layer = value["render_layer"].get<unsigned>();
                if (!handle || layer > 31) return false;
                assignments.push_back({*handle, static_cast<RenderLayer>(layer), value.value("enabled", true)});
            }
        }
        m_scene->set_render_layer_effect_assignments(std::move(assignments));

        EntityManager candidate;
        const ComponentSerializationContext component_context{m_renderer, &AssetManager::get(), &AssetDatabase::get()};
        std::vector<EntityID> loaded_entities;
        std::vector<std::pair<EntityID, EntityID>> hierarchy_links;
        std::vector<std::pair<EntityID, std::vector<std::string>>> editor_component_orders;

        if (root.contains("editor_metadata")) {
            const auto& metadata = root["editor_metadata"];
            if (!metadata.is_object() || !metadata.contains("component_orders") ||
                !metadata["component_orders"].is_array()) {
                ENGINE_LOG_ERROR("[SERIALIZER] Scene editor_metadata must contain a component_orders array");
                return false;
            }
            std::unordered_set<EntityID> ordered_entities;
            for (const auto& entry : metadata["component_orders"]) {
                if (!entry.is_object() || !entry.contains("entity") ||
                    !entry["entity"].is_number_unsigned() || !entry.contains("components") ||
                    !entry["components"].is_array()) {
                    ENGINE_LOG_ERROR("[SERIALIZER] Scene component order entry is invalid");
                    return false;
                }
                const EntityID entity = entry["entity"].get<EntityID>();
                if (entity == 0 || !ordered_entities.insert(entity).second) {
                    ENGINE_LOG_ERROR("[SERIALIZER] Scene component order has an invalid or duplicate entity ID");
                    return false;
                }
                std::vector<std::string> order;
                std::unordered_set<std::string> keys;
                for (const auto& component : entry["components"]) {
                    if (!component.is_string()) {
                        ENGINE_LOG_ERROR("[SERIALIZER] Scene component order keys must be strings");
                        return false;
                    }
                    std::string key = component.get<std::string>();
                    if (!registry.find_by_key(key) || !keys.insert(key).second) {
                        ENGINE_LOG_ERROR("[SERIALIZER] Scene component order contains unknown or duplicate key '{}'", key);
                        return false;
                    }
                    order.push_back(std::move(key));
                }
                editor_component_orders.emplace_back(entity, std::move(order));
            }
        }

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
            for (const auto& [entity, order] : editor_component_orders) {
                (void)order;
                if (!candidate.is_alive(entity)) {
                    ENGINE_LOG_ERROR("[SERIALIZER] Component order references missing entity #{}", entity);
                    return false;
                }
            }
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
        if (m_editor_metadata) {
            m_editor_metadata->component_orders.clear();
            for (auto& [entity, order] : editor_component_orders) {
                m_editor_metadata->component_orders.emplace(entity, std::move(order));
            }
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
