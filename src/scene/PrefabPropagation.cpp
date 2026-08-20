#include "PrefabPropagation.hpp"
#include "PrefabSerializer.hpp"
#include "EntitySubtreeArchive.hpp"
#include "../core/AssetManager.hpp"
#include "../core/Log.hpp"
#include "../ecs/TransformSystem.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include <functional>
#include <vector>

namespace Engine {

std::vector<EntityID> PrefabPropagation::propagate_to_instances(
    EntityManager& em,
    const std::string& prefab_asset_path,
    EntityID skip_entity,
    SDL_Renderer* renderer
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
        if (id != skip_entity && link.prefab_asset_path == prefab_asset_path) {
            target_roots.push_back(id);
        }
    });

    if (target_roots.empty()) {
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

        // Destroy old subtree
        for (auto it = subtree_ids.rbegin(); it != subtree_ids.rend(); ++it) {
            if (em.is_alive(*it)) {
                em.destroy_entity(*it);
            }
        }

        // Instantiate fresh from asset JSON
        ComponentSerializationContext context{
            .renderer = renderer,
            .asset_manager = &AssetManager::get()
        };
        auto result = EntitySubtreeArchive::instantiate_from_json(
            em,
            json_content,
            desired_world,
            parent_id,
            context
        );

        if (result.success) {
            if (!em.has_component<PrefabLinkComponent>(result.root_entity)) {
                em.add_component<PrefabLinkComponent>(result.root_entity, PrefabLinkComponent{prefab_asset_path, result.base_prefab_path});
            } else {
                auto& l = em.get_component<PrefabLinkComponent>(result.root_entity);
                l.prefab_asset_path = prefab_asset_path;
                l.base_prefab_path = result.base_prefab_path;
            }
            new_roots.push_back(result.root_entity);
        } else {
            ENGINE_LOG_ERROR("[PREFAB] PrefabPropagation failed instantiating instance: {}", result.error_message);
        }

    }

    TransformSystem::update(em);
    ENGINE_LOG_INFO("[PREFAB] Propagated '{}' to {} scene instances", prefab_asset_path, new_roots.size());
    return new_roots;
}

} // namespace Engine
