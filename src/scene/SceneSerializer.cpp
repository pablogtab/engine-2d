#include "SceneSerializer.hpp"
#include "ComponentRegistry.hpp"
#include "../core/FileSystem.hpp"
#include "../core/Log.hpp"
#include "../ecs/Components.hpp"
#include "../tilemap/Tilemap.hpp"
#include "../core/AssetManager.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Engine {

SceneSerializer::SceneSerializer(Scene* scene, SDL_Renderer* renderer)
    : m_scene(scene), m_renderer(renderer) {}

std::string SceneSerializer::serialize_to_string() {
    if (!m_scene) {
        ENGINE_LOG_ERROR("[SERIALIZER] Cannot serialize: Scene pointer is null");
        return "";
    }

    json root;
    root["scene_name"] = m_scene->get_name();
    root["version"] = 1;

    json entities_array = json::array();
    EntityManager& em = m_scene->get_entity_manager();
    const ComponentSerializationContext component_context{m_renderer, &AssetManager::get()};
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

        if (!default_component_registry().serialize_entity(
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
        EntityManager& em = m_scene->get_entity_manager();
        em.clear();
        const ComponentSerializationContext component_context{m_renderer, &AssetManager::get()};
        std::vector<EntityID> loaded_entities;
        std::vector<std::pair<EntityID, EntityID>> hierarchy_links;

        if (root.contains("entities") && root["entities"].is_array()) {
            for (const auto& entity_json : root["entities"]) {
                const EntityID serialized_id = entity_json.value("id", EntityID{0});
                const EntityID id = serialized_id != 0
                    ? em.create_entity_with_id(serialized_id)
                    : em.create_entity();
                if (id == 0) {
                    ENGINE_LOG_ERROR("[SERIALIZER] Duplicate or invalid entity ID {}", serialized_id);
                    return false;
                }

                std::string component_error;
                if (!default_component_registry().deserialize_entity(
                        em, id, entity_json, component_context, component_error
                    )) {
                    ENGINE_LOG_ERROR("[SERIALIZER] Entity {}: {}", id, component_error);
                    return false;
                }
                loaded_entities.push_back(id);

                // Entity references may point forward in the JSON array, so
                // create every entity before resolving any parent relation.
                if (entity_json.contains("hierarchy") && entity_json["hierarchy"].is_object()) {
                    const auto& hierarchy_json = entity_json["hierarchy"];
                    em.add_component<HierarchyComponent>(id, HierarchyComponent{});
                    hierarchy_links.emplace_back(id, hierarchy_json.value("parent", EntityID{0}));
                }

            }

            // Rebuild children from the authoritative parent links only after
            // every serialized entity exists. This is also what makes the
            // Play/Stop snapshot restore entities destroyed during simulation
            // without losing their authoring hierarchy.
            for (const auto& [child, parent] : hierarchy_links) {
                if (parent == 0) continue;
                if (!em.is_alive(parent)) {
                    ENGINE_LOG_WARN(
                        "[SERIALIZER] Entity #{} references missing parent #{}; restored as root",
                        child,
                        parent
                    );
                    continue;
                }
                em.set_parent(child, parent);
            }

            const ComponentFinalizeContext finalize_context{
                .serialization = component_context,
                .reason = ComponentFinalizeReason::Deserialization
            };
            std::string finalize_error;
            if (!default_component_registry().finalize_entities(
                    em, loaded_entities, finalize_context, finalize_error
                )) {
                ENGINE_LOG_ERROR("[SERIALIZER] Component finalization failed: {}", finalize_error);
                return false;
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
