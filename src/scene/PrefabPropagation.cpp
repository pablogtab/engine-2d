#include "PrefabPropagation.hpp"
#include "PrefabSerializer.hpp"
#include "EntitySubtreeArchive.hpp"
#include "ComponentRegistry.hpp"
#include "../core/AssetManager.hpp"
#include "../core/Log.hpp"
#include "../ecs/TransformSystem.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <nlohmann/json.hpp>

namespace Engine {

namespace {

bool synchronize_clean_instance(
    EntityManager& em,
    EntityID old_root,
    const std::vector<EntityID>& old_subtree_ids,
    const std::string& prefab_json,
    const TransformComponent& desired_world,
    EntityID original_parent,
    const std::string& prefab_asset_path,
    SDL_Renderer* renderer
) {
    nlohmann::json document;
    try {
        document = nlohmann::json::parse(prefab_json);
        if (!document.contains("entities") || !document["entities"].is_array()) return false;
    } catch (...) {
        return false;
    }

    // Validate and finalize a complete replacement away from the scene first.
    EntityManager staging_em;
    ComponentSerializationContext context{.renderer = renderer, .asset_manager = &AssetManager::get()};
    const auto staged = EntitySubtreeArchive::instantiate_from_json(
        staging_em, prefab_json, std::nullopt, 0, context
    );
    if (!staged.success || staged.created_entities.size() != document["entities"].size()) {
        ENGINE_LOG_WARN("[PREFAB] Propagation staging failed for instance #{}: {}", old_root, staged.error_message);
        return false;
    }

    std::unordered_map<EntityID, EntityID> old_local_to_runtime;
    for (std::size_t index = 0; index < old_subtree_ids.size(); ++index) {
        // capture_to_json assigns the same breadth-first local IDs.
        old_local_to_runtime.emplace(static_cast<EntityID>(index + 1), old_subtree_ids[index]);
    }
    std::unordered_map<EntityID, EntityID> staged_local_to_runtime;
    for (std::size_t index = 0; index < document["entities"].size(); ++index) {
        staged_local_to_runtime.emplace(
            document["entities"][index]["id"].get<EntityID>(), staged.created_entities[index]
        );
    }

    const ComponentRegistry& registry = default_component_registry();
    std::unordered_map<EntityID, ComponentSnapshotSet> previous_snapshots;
    std::unordered_map<EntityID, EntityID> previous_parents;
    for (const EntityID id : old_subtree_ids) {
        ComponentSnapshotSet snapshot;
        registry.capture_entity(em, id, snapshot);
        previous_snapshots.emplace(id, std::move(snapshot));
        previous_parents.emplace(
            id,
            em.has_component<HierarchyComponent>(id) ? em.get_component<HierarchyComponent>(id).parent : 0
        );
    }

    std::unordered_map<EntityID, EntityID> staged_to_live;
    std::vector<EntityID> created_live;
    for (const auto& [local_id, staged_id] : staged_local_to_runtime) {
        const auto existing = old_local_to_runtime.find(local_id);
        const EntityID live_id = existing == old_local_to_runtime.end() ? em.create_entity() : existing->second;
        if (live_id == 0) return false;
        if (existing == old_local_to_runtime.end()) created_live.push_back(live_id);
        staged_to_live.emplace(staged_id, live_id);
    }

    auto rollback = [&]() {
        for (const auto& [id, snapshot] : previous_snapshots) {
            if (em.is_alive(id)) registry.restore_entity(em, id, snapshot);
        }
        for (const EntityID id : created_live) {
            if (em.is_alive(id)) em.destroy_entity(id);
        }
        for (const auto& [id, parent] : previous_parents) {
            if (!em.is_alive(id)) continue;
            if (parent != 0 && em.is_alive(parent)) {
                static_cast<void>(em.set_parent(id, parent));
            } else {
                static_cast<void>(em.remove_parent(id));
            }
        }
        TransformSystem::update(em);
    };

    for (const auto& [local_id, staged_id] : staged_local_to_runtime) {
        const EntityID live_id = staged_to_live.at(staged_id);
        ComponentSnapshotSet snapshot;
        registry.capture_entity(staging_em, staged_id, snapshot);
        registry.restore_entity(em, live_id, snapshot);
    }

    // Recreate the prefab hierarchy against the stable scene IDs.
    for (const auto& entity_json : document["entities"]) {
        const EntityID local_id = entity_json["id"].get<EntityID>();
        const EntityID staged_id = staged_local_to_runtime.at(local_id);
        const EntityID live_id = staged_to_live.at(staged_id);
        const EntityID local_parent = entity_json.value("hierarchy", nlohmann::json::object()).value("parent", EntityID{0});
        if (local_parent == 0) {
            if (original_parent != 0 && em.is_alive(original_parent)) {
                if (!em.set_parent(live_id, original_parent).succeeded()) { rollback(); return false; }
            } else {
                static_cast<void>(em.remove_parent(live_id));
            }
        } else {
            const EntityID staged_parent = staged_local_to_runtime.at(local_parent);
            if (!em.set_parent(live_id, staged_to_live.at(staged_parent)).succeeded()) { rollback(); return false; }
        }
    }

    // A scene placement is not an asset override. Preserve the old root world
    // transform while replacing its authored local contents.
    if (em.has_component<TransformComponent>(old_root)) {
        if (original_parent != 0 && em.is_alive(original_parent)) {
            const auto local = TransformSystem::world_to_local_transform(
                desired_world, &em.get_component<TransformComponent>(original_parent)
            );
            if (!local.has_value()) { rollback(); return false; }
            em.get_component<TransformComponent>(old_root) = *local;
        } else {
            em.get_component<TransformComponent>(old_root) = desired_world;
        }
    }

    for (const auto& [staged_id, live_id] : staged_to_live) {
        registry.remap_entity_references(em, live_id, staged_to_live);
    }
    if (!em.has_component<PrefabLinkComponent>(old_root)) {
        em.add_component<PrefabLinkComponent>(old_root, PrefabLinkComponent{prefab_asset_path, staged.base_prefab_path});
    } else {
        auto& link = em.get_component<PrefabLinkComponent>(old_root);
        link.prefab_asset_path = prefab_asset_path;
        link.base_prefab_path = staged.base_prefab_path;
    }
    for (const auto& [local_id, staged_id] : staged_local_to_runtime) {
        const EntityID live_id = staged_to_live.at(staged_id);
        if (live_id == old_root || em.has_component<PrefabLinkComponent>(live_id)) continue;
        em.add_component<PrefabLinkComponent>(
            live_id,
            PrefabLinkComponent{prefab_asset_path, staged.base_prefab_path, local_id, false}
        );
    }

    std::vector<EntityID> finalized_entities;
    finalized_entities.reserve(staged_to_live.size());
    for (const auto& [staged_id, live_id] : staged_to_live) finalized_entities.push_back(live_id);
    std::string finalize_error;
    if (!registry.finalize_entities(
            em,
            finalized_entities,
            {.serialization = context, .reason = ComponentFinalizeReason::DeserializationCommit},
            finalize_error
        )) {
        ENGINE_LOG_WARN("[PREFAB] Propagation rollback for instance #{}: {}", old_root, finalize_error);
        rollback();
        return false;
    }

    std::unordered_set<EntityID> retained;
    for (const auto& [staged_id, live_id] : staged_to_live) retained.insert(live_id);
    for (auto it = old_subtree_ids.rbegin(); it != old_subtree_ids.rend(); ++it) {
        if (!retained.contains(*it) && em.is_alive(*it)) em.destroy_entity(*it);
    }
    TransformSystem::update(em);
    return true;
}

} // namespace

std::vector<EntityID> PrefabPropagation::propagate_to_instances(
    EntityManager& em,
    const std::string& prefab_asset_path,
    EntityID skip_entity,
    SDL_Renderer* renderer,
    const std::string& baseline_prefab_json
) {
    if (prefab_asset_path.empty()) return {};

    std::string json_content = PrefabSerializer::read_json_from_file(prefab_asset_path);
    if (json_content.empty()) {
        ENGINE_LOG_WARN("[PREFAB] PrefabPropagation: cannot read asset JSON from '{}'", prefab_asset_path);
        return {};
    }

    // 1. Collect all root entities matching this prefab asset path
    std::vector<EntityID> target_roots;
    em.for_each<PrefabLinkComponent>([&](EntityID id, const PrefabLinkComponent& link) {
        if (id != skip_entity && link.is_root_instance && link.prefab_asset_path == prefab_asset_path) {
            target_roots.push_back(id);
        }
    });

    if (target_roots.empty()) {
        return {};
    }

    // A propagation without the previous asset baseline cannot distinguish asset
    // changes from per-instance overrides. Refuse it rather than silently deleting
    // authored instance data.
    if (baseline_prefab_json.empty()) {
        ENGINE_LOG_WARN("[PREFAB] Propagation of '{}' skipped: no baseline available to preserve instance overrides", prefab_asset_path);
        return {};
    }


    std::vector<EntityID> new_roots;
    new_roots.reserve(target_roots.size());

    // 2. Replace each matching instance
    for (EntityID old_root : target_roots) {
        if (!em.is_alive(old_root)) continue;

        // Collect existing subtree in hierarchical order
        std::vector<EntityID> subtree_ids;
        std::function<void(EntityID)> collect = [&](EntityID id) {
            if (!em.is_alive(id)) return;
            subtree_ids.push_back(id);
            if (em.has_component<HierarchyComponent>(id)) {
                for (EntityID child : em.get_component<HierarchyComponent>(id).children) {
                    collect(child);
                }
            }
        };
        collect(old_root);

        ComponentSerializationContext capture_context{
            .renderer = renderer,
            .asset_manager = &AssetManager::get()
        };
        const std::string instance_json = EntitySubtreeArchive::capture_to_json(em, old_root, capture_context);
        if (instance_json.empty()) {
            ENGINE_LOG_WARN("[PREFAB] Propagation skipped instance #{}: cannot capture override state", old_root);
            continue;
        }

        try {
            if (nlohmann::json::parse(instance_json) != nlohmann::json::parse(baseline_prefab_json)) {
                ENGINE_LOG_INFO("[PREFAB] Propagation preserved overridden instance #{}", old_root);
                continue;
            }
        } catch (const std::exception& e) {
            ENGINE_LOG_WARN("[PREFAB] Propagation skipped instance #{}: invalid baseline ({})", old_root, e.what());
            continue;
        }

        // Capture world transform & parent
        TransformComponent desired_world;
        if (em.has_component<TransformComponent>(old_root)) {
            const auto& tr = em.get_component<TransformComponent>(old_root);
            desired_world.position = tr.world_position;
            desired_world.scale = tr.world_scale;
            desired_world.rotation = tr.world_rotation;
            desired_world.pivot = tr.world_pivot;
            desired_world.world_position = tr.world_position;
            desired_world.world_scale = tr.world_scale;
            desired_world.world_rotation = tr.world_rotation;
            desired_world.world_pivot = tr.world_pivot;
        }

        EntityID parent_id = 0;
        if (em.has_component<HierarchyComponent>(old_root)) {
            parent_id = em.get_component<HierarchyComponent>(old_root).parent;
        }

        if (synchronize_clean_instance(
                em, old_root, subtree_ids, json_content, desired_world, parent_id,
                prefab_asset_path, renderer
            )) {
            new_roots.push_back(old_root);
        } else {
            ENGINE_LOG_ERROR("[PREFAB] Propagation failed synchronizing instance #{}", old_root);
        }

    }

    TransformSystem::update(em);
    ENGINE_LOG_INFO("[PREFAB] Propagated '{}' to {} scene instances", prefab_asset_path, new_roots.size());
    return new_roots;
}

} // namespace Engine
