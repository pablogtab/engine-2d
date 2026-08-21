#include "EntityCommands.hpp"
#include "../state/EditorEntityStateStore.hpp"
#include "../tools/EditorTransformUtils.hpp"
#include "../../core/Log.hpp"
#include "../../ecs/TransformSystem.hpp"
#include "../../physics/PhysicsSystem.hpp"
#include "../../tilemap/Tilemap.hpp"
#include "../../tilemap/TilemapColliderSystem.hpp"

namespace Engine {

SetRegisteredComponentCommand::SetRegisteredComponentCommand(
    ComponentRegistry* registry,
    EntityManager* em,
    EntityID entity,
    std::string component_key,
    ComponentSnapshotSet before,
    ComponentSnapshotSet after,
    ComponentSerializationContext serialization_context,
    std::string description,
    std::shared_ptr<EditorEntityStateStore> state_store,
    std::vector<std::string> before_order,
    std::vector<std::string> after_order
) : m_registry(registry),
    m_em(em),
    m_entity(entity),
    m_component_key(std::move(component_key)),
    m_before(std::move(before)),
    m_after(std::move(after)),
    m_serialization_context(serialization_context),
    m_description(std::move(description)),
    m_state_store(std::move(state_store)),
    m_before_order(std::move(before_order)),
    m_after_order(std::move(after_order)) {}

bool SetRegisteredComponentCommand::execute() {
    return apply(m_after, m_after_order);
}

bool SetRegisteredComponentCommand::undo() {
    return apply(m_before, m_before_order);
}

bool SetRegisteredComponentCommand::apply(
    const ComponentSnapshotSet& snapshot,
    const std::vector<std::string>& component_order
) {
    if (!m_registry || !m_em || !m_em->is_alive(m_entity)) return false;
    const ComponentDescriptor* descriptor = m_registry->find_by_key(m_component_key);
    if (!descriptor) {
        ENGINE_LOG_ERROR("[EDITOR COMMAND] Unknown component '{}'.", m_component_key);
        return false;
    }
    descriptor->restore(*m_em, m_entity, snapshot);
    if (!descriptor->has(*m_em, m_entity) || !descriptor->finalize) {
        if (m_state_store) m_state_store->set_component_order(m_entity, component_order);
        return true;
    }

    std::string error;
    if (!descriptor->finalize(
            *m_em,
            m_entity,
            ComponentFinalizeContext{
                .serialization = m_serialization_context,
                .reason = ComponentFinalizeReason::EditorComponentChange
            },
            error
        )) {
        ENGINE_LOG_ERROR(
            "[EDITOR COMMAND] Failed to finalize component '{}' on entity #{}: {}",
            m_component_key,
            m_entity,
            error
        );
        return false;
    }
    if (m_state_store) m_state_store->set_component_order(m_entity, component_order);
    return true;
}

EntitySnapshot capture_entity_snapshot(EntityManager& em, EntityID entity) {
    EntitySnapshot snapshot;
    snapshot.id = entity;
    if (!em.is_alive(entity)) return snapshot;

    if (em.has_component<HierarchyComponent>(entity)) { snapshot.has_hierarchy = true; snapshot.hierarchy = em.get_component<HierarchyComponent>(entity); }
    default_component_registry().capture_entity(em, entity, snapshot.registered_components);
    return snapshot;
}

static void attach_snapshot_components(EntityManager& em, const EntitySnapshot& snapshot) {
    default_component_registry().restore_entity(em, snapshot.id, snapshot.registered_components);
    if (snapshot.has_hierarchy) em.add_component<HierarchyComponent>(snapshot.id, HierarchyComponent{});
}

static std::string snapshot_tag(const EntitySnapshot& snapshot) {
    const auto* tag = snapshot.registered_components.get<TagComponent>();
    return tag ? tag->tag : "Entity";
}

static bool finalize_snapshot_components(EntityManager& em, EntityID entity) {
    std::string error;
    return default_component_registry().finalize_entity(
        em,
        entity,
        ComponentFinalizeContext{.reason = ComponentFinalizeReason::SnapshotRestore},
        error
    );
}

bool restore_entity_snapshot(EntityManager& em, const EntitySnapshot& snapshot) {
    if (snapshot.id == 0 || em.is_alive(snapshot.id) || em.create_entity_with_id(snapshot.id) == 0) return false;
    attach_snapshot_components(em, snapshot);
    return true;
}

// ---------------------------------------------------------------------------
// Generic inspector/property edit command
// ---------------------------------------------------------------------------
ModifyEntityCommand::ModifyEntityCommand(
    EntityManager* em,
    EntityID entity,
    Operation apply_before,
    Operation apply_after,
    std::string description
) : m_em(em),
    m_entity(entity),
    m_apply_before(std::move(apply_before)),
    m_apply_after(std::move(apply_after)),
    m_description(std::move(description)) {}

bool ModifyEntityCommand::execute() {
    if (m_em && m_em->is_alive(m_entity) && m_apply_after) {
        m_apply_after(*m_em, m_entity);
        return true;
    }
    return false;
}

bool ModifyEntityCommand::undo() {
    if (m_em && m_em->is_alive(m_entity) && m_apply_before) {
        m_apply_before(*m_em, m_entity);
        return true;
    }
    return false;
}

TransformEntitiesCommand::TransformEntitiesCommand(
    EntityManager* em,
    std::unordered_map<EntityID, TransformComponent> before,
    std::unordered_map<EntityID, TransformComponent> after,
    std::string description,
    const EditorEntityStateStore* state_store
) : m_em(em), m_before(std::move(before)), m_after(std::move(after)), m_description(std::move(description)) {
    if (state_store) {
        for (const auto& [id, _] : m_before) {
            if (state_store->is_locked(id)) {
                ENGINE_LOG_WARN("[COMMAND] Cannot transform locked entity #{}", id);
                m_valid = false;
                return;
            }
        }
    }
}

bool TransformEntitiesCommand::apply(const std::unordered_map<EntityID, TransformComponent>& states) {
    if (!m_valid || !m_em) return false;
    std::unordered_set<EntityID> rebuild_roots;
    for (const auto& [id, transform] : states) {
        if (!m_em->is_alive(id) || !m_em->has_component<TransformComponent>(id)) continue;
        const auto& current = m_em->get_component<TransformComponent>(id);
        if (current.scale != transform.scale || current.visible != transform.visible) {
            rebuild_roots.insert(id);
        }
        m_em->get_component<TransformComponent>(id) = transform;
    }

    TransformSystem::update(*m_em);
    for (const auto& [id, transform] : states) {
        (void)transform;
        if (!m_em->is_alive(id) || !m_em->has_component<TransformComponent>(id)) continue;
        if (rebuild_roots.contains(id)) {
            PhysicsSystem::rebuild_body_hierarchy(*m_em, id);
        } else {
            PhysicsSystem::sync_body_hierarchy(*m_em, id);
        }
    }
    return true;
}

bool TransformEntitiesCommand::execute() { return apply(m_after); }
bool TransformEntitiesCommand::undo() { return apply(m_before); }

SetPrimaryCameraCommand::SetPrimaryCameraCommand(EntityManager* em, EntityID primary_camera)
    : m_em(em) {
    if (!m_em || !m_em->is_alive(primary_camera) || !m_em->has_component<CameraComponent>(primary_camera)) return;
    m_em->for_each<CameraComponent>([&](EntityID id, const CameraComponent& camera) {
        m_before[id] = camera;
        CameraComponent updated = camera;
        updated.is_primary = id == primary_camera;
        m_after[id] = updated;
    });
}

bool SetPrimaryCameraCommand::apply(const std::unordered_map<EntityID, CameraComponent>& states) {
    if (!m_em) return false;
    for (const auto& [id, camera] : states) {
        if (m_em->is_alive(id) && m_em->has_component<CameraComponent>(id)) {
            m_em->get_component<CameraComponent>(id) = camera;
        }
    }
    return true;
}

bool SetPrimaryCameraCommand::execute() { return apply(m_after); }
bool SetPrimaryCameraCommand::undo() { return apply(m_before); }

static void collect_subtree_ids(const EntityManager& em, EntityID root, std::unordered_set<EntityID>& out) {
    if (!em.is_alive(root)) return;
    out.insert(root);
    if (em.has_component<HierarchyComponent>(root)) {
        for (EntityID child : em.get_component<HierarchyComponent>(root).children) {
            collect_subtree_ids(em, child, out);
        }
    }
}

// ---------------------------------------------------------------------------
// 2. Delete Entities Command
// ---------------------------------------------------------------------------
DeleteEntitiesCommand::DeleteEntitiesCommand(
    EntityManager* em,
    std::shared_ptr<EditorSelection> selection,
    std::vector<EntityID> entities_to_delete,
    EditorEntityStateStore* state_store
) : m_em(em), m_selection(selection), m_state_store(state_store) {
    if (!m_em || entities_to_delete.empty()) {
        m_valid = false;
        return;
    }

    std::unordered_set<EntityID> all_affected_ids;
    for (EntityID id : entities_to_delete) {
        collect_subtree_ids(*m_em, id, all_affected_ids);
    }

    if (state_store) {
        for (EntityID id : all_affected_ids) {
            if (state_store->is_locked(id)) {
                ENGINE_LOG_WARN("[COMMAND] Cannot delete entity #{} because entity #{} in subtree is locked", id, id);
                m_valid = false;
                return;
            }
        }
    }

    for (EntityID id : all_affected_ids) {
        if (!m_em->is_alive(id)) continue;

        if (state_store) {
            m_editor_states[id] = state_store->get(id);
        }
        m_snapshots.push_back(capture_entity_snapshot(*m_em, id));
    }

    if (m_snapshots.empty()) {
        m_valid = false;
        return;
    }

    if (entities_to_delete.size() == 1) {
        m_description = "Delete " + snapshot_tag(capture_entity_snapshot(*m_em, entities_to_delete[0]));
    } else {
        m_description = "Delete (" + std::to_string(entities_to_delete.size()) + ") Entities";
    }
}

bool DeleteEntitiesCommand::execute() {
    if (!m_valid || !m_em) return false;

    for (const auto& snap : m_snapshots) {
        if (m_em->is_alive(snap.id)) {
            if (m_em->has_component<TilemapComponent>(snap.id)) {
                TilemapColliderSystem::clear_composite_colliders(*m_em, snap.id);
            }
            m_em->destroy_entity(snap.id);
        }
    }

    if (m_state_store) {
        for (const auto& [id, _] : m_editor_states) {
            m_state_store->remove(id);
        }
    }

    if (m_selection) {
        m_selection->clear();
    }
    return true;
}

bool DeleteEntitiesCommand::undo() {
    if (!m_valid || !m_em) return false;

    std::unordered_set<EntityID> restored_ids;

    for (const auto& snap : m_snapshots) {
        if (restore_entity_snapshot(*m_em, snap)) restored_ids.insert(snap.id);
    }

    // Reconnect hierarchy only after every entity has been restored.
    for (const auto& snap : m_snapshots) {
        if (!snap.has_hierarchy || !m_em->is_alive(snap.id)) continue;
        if (snap.hierarchy.parent != 0 && m_em->is_alive(snap.hierarchy.parent)) {
            const HierarchyOperationResult result = m_em->set_parent(snap.id, snap.hierarchy.parent);
            if (!result.succeeded()) {
                ENGINE_LOG_ERROR(
                    "[EDITOR COMMAND] Cannot restore parent #{} for entity #{}: {}",
                    snap.hierarchy.parent,
                    snap.id,
                    result.message()
                );
            }
        }
        for (EntityID child : snap.hierarchy.children) {
            if (!m_em->is_alive(child)) continue;
            const HierarchyOperationResult result = m_em->set_parent(child, snap.id);
            if (!result.succeeded()) {
                ENGINE_LOG_ERROR(
                    "[EDITOR COMMAND] Cannot restore child #{} for entity #{}: {}",
                    child,
                    snap.id,
                    result.message()
                );
            }
        }
    }

    for (const auto& snap : m_snapshots) {
        if (m_em->is_alive(snap.id)) finalize_snapshot_components(*m_em, snap.id);
    }

    if (m_state_store) {
        for (const auto& [id, state] : m_editor_states) {
            m_state_store->set(id, state);
        }
    }

    if (m_selection) {
        m_selection->set_selection(restored_ids);
    }
    return true;
}

// ---------------------------------------------------------------------------
// 3. Create Entity Command
// ---------------------------------------------------------------------------
CreateEntityCommand::CreateEntityCommand(
    EntityManager* em,
    std::shared_ptr<EditorSelection> selection,
    EntitySnapshot snapshot,
    const EditorEntityStateStore* state_store
) : m_em(em), m_selection(selection), m_snapshot(std::move(snapshot)) {
    if (m_snapshot.has_hierarchy && m_snapshot.hierarchy.parent != 0 && state_store) {
        if (state_store->is_locked(m_snapshot.hierarchy.parent)) {
            ENGINE_LOG_WARN("[COMMAND] Cannot create child entity under locked parent #{}", m_snapshot.hierarchy.parent);
            m_valid = false;
            return;
        }
    }
    m_description = "Create " + snapshot_tag(m_snapshot);
}

bool CreateEntityCommand::execute() {
    if (!m_valid || !m_em) return false;

    if (m_snapshot.id == 0) {
        m_snapshot.id = m_em->create_entity();
        attach_snapshot_components(*m_em, m_snapshot);
    } else if (!restore_entity_snapshot(*m_em, m_snapshot)) {
        return false;
    }
    m_created_id = m_snapshot.id;

    if (m_snapshot.has_hierarchy && m_snapshot.hierarchy.parent != 0 && m_em->is_alive(m_snapshot.hierarchy.parent)) {
        const HierarchyOperationResult result = m_em->set_parent(
            m_created_id,
            m_snapshot.hierarchy.parent
        );
        if (!result.succeeded()) {
            ENGINE_LOG_ERROR(
                "[EDITOR COMMAND] Cannot parent created entity #{} to #{}: {}",
                m_created_id,
                m_snapshot.hierarchy.parent,
                result.message()
            );
            m_em->destroy_entity(m_created_id);
            m_created_id = 0;
            return false;
        }
    }

    if (!finalize_snapshot_components(*m_em, m_created_id)) {
        m_em->destroy_entity(m_created_id);
        m_created_id = 0;
        return false;
    }

    if (m_selection) {
        m_selection->select(m_created_id, false);
    }
    return true;
}

bool CreateEntityCommand::undo() {
    if (!m_em) return false;

    if (m_created_id != 0 && m_em->is_alive(m_created_id)) {
        if (m_em->has_component<TilemapComponent>(m_created_id)) {
            TilemapColliderSystem::clear_composite_colliders(*m_em, m_created_id);
        }
        m_em->destroy_entity(m_created_id);
    }

    if (m_selection && m_selection->is_selected(m_created_id)) {
        m_selection->clear();
    }
    return true;
}

// ---------------------------------------------------------------------------
// 3.5 Duplicate Entities Command
// ---------------------------------------------------------------------------
// 3.5 Duplicate Entities Command (Recursive Subtree Duplication)
// ---------------------------------------------------------------------------
static void collect_subtree_ids_ordered(const EntityManager& em, EntityID root, std::vector<EntityID>& out) {
    if (!em.is_alive(root)) return;
    out.push_back(root);
    if (em.has_component<HierarchyComponent>(root)) {
        for (EntityID child : em.get_component<HierarchyComponent>(root).children) {
            collect_subtree_ids_ordered(em, child, out);
        }
    }
}

DuplicateEntitiesCommand::DuplicateEntitiesCommand(
    EntityManager* em,
    std::shared_ptr<EditorSelection> selection,
    std::vector<EntityID> source_entities,
    const EditorEntityStateStore* state_store
) : m_em(em), m_selection(std::move(selection)), m_source_entities(std::move(source_entities)) {
    if (!m_em || m_source_entities.empty()) {
        m_valid = false;
        return;
    }

    TransformSystem::update(*m_em);

    const std::unordered_set<EntityID> source_set(m_source_entities.begin(), m_source_entities.end());

    // Filter to selected roots only (entities that don't have a selected ancestor in the group)
    for (EntityID id : m_source_entities) {
        if (m_em->is_alive(id) && !has_selected_ancestor(*m_em, id, source_set)) {
            m_source_roots.push_back(id);
        }
    }

    if (m_source_roots.empty()) {
        m_valid = false;
        return;
    }

    // Verify locks across all subtrees and any unselected parent of a root
    if (state_store) {
        for (EntityID root_id : m_source_roots) {
            if (m_em->has_component<HierarchyComponent>(root_id)) {
                EntityID parent = m_em->get_component<HierarchyComponent>(root_id).parent;
                if (parent != 0 && state_store->is_locked(parent)) {
                    ENGINE_LOG_WARN("[COMMAND] Cannot duplicate entity #{} under locked parent #{}", root_id, parent);
                    m_valid = false;
                    return;
                }
            }
            std::vector<EntityID> subtree;
            collect_subtree_ids_ordered(*m_em, root_id, subtree);
            for (EntityID id : subtree) {
                if (state_store->is_locked(id)) {
                    ENGINE_LOG_WARN("[COMMAND] Cannot duplicate entity #{} because entity #{} in its subtree is locked", root_id, id);
                    m_valid = false;
                    return;
                }
            }
        }
    }

    if (m_source_roots.size() == 1) {
        m_description = "Duplicate " + snapshot_tag(capture_entity_snapshot(*m_em, m_source_roots[0]));
    } else {
        m_description = "Duplicate (" + std::to_string(m_source_roots.size()) + ") Entities";
    }
}

bool DuplicateEntitiesCommand::execute() {
    if (!m_valid || !m_em) return false;

    auto rollback_created = [this](const std::vector<EntityID>& created_ids) {
        for (auto it = created_ids.rbegin(); it != created_ids.rend(); ++it) {
            if (m_em->is_alive(*it)) {
                if (m_em->has_component<TilemapComponent>(*it)) {
                    TilemapColliderSystem::clear_composite_colliders(*m_em, *it);
                }
                m_em->destroy_entity(*it);
            }
        }
        TransformSystem::update(*m_em);
        if (m_selection) {
            std::unordered_set<EntityID> sources;
            for (EntityID id : m_source_entities) {
                if (m_em->is_alive(id)) sources.insert(id);
            }
            m_selection->set_selection(sources);
        }
    };

    if (!m_executed_once) {
        m_cloned_snapshots.clear();
        m_new_root_ids.clear();

        // 1. Gather all subtrees and pre-allocate new EntityIDs
        std::unordered_map<EntityID, EntityID> old_to_new;
        std::unordered_set<EntityID> root_set(m_source_roots.begin(), m_source_roots.end());

        std::vector<EntityID> all_entities_ordered;
        std::vector<EntityID> all_new_ids;
        for (EntityID root_id : m_source_roots) {
            std::vector<EntityID> subtree;
            collect_subtree_ids_ordered(*m_em, root_id, subtree);
            for (EntityID id : subtree) {
                const EntityID new_id = m_em->create_entity();
                old_to_new[id] = new_id;
                all_entities_ordered.push_back(id);
                all_new_ids.push_back(new_id);
                if (id == root_id) {
                    m_new_root_ids.push_back(new_id);
                }
            }
        }

        // 2. Build snapshots with remapped hierarchy and offset applied to roots
        for (EntityID orig_id : all_entities_ordered) {
            EntitySnapshot snap = capture_entity_snapshot(*m_em, orig_id);
            const EntityID new_id = old_to_new[orig_id];
            snap.id = new_id;
            const bool is_root = root_set.contains(orig_id);

            if (is_root) {
                if (auto* tag = snap.registered_components.get<TagComponent>()) {
                    tag->tag += "_Copy";
                }
                if (auto* transform = snap.registered_components.get<TransformComponent>()) {
                    TransformComponent target_world = *transform;
                    target_world.world_position += glm::vec3(30.0f, 30.0f, 0.0f);
                    const TransformComponent* parent_world = (snap.has_hierarchy && snap.hierarchy.parent != 0 && m_em->has_component<TransformComponent>(snap.hierarchy.parent))
                        ? &m_em->get_component<TransformComponent>(snap.hierarchy.parent)
                        : nullptr;
                    auto local_opt = TransformSystem::world_to_local_transform(target_world, parent_world);
                    if (local_opt) {
                        *transform = *local_opt;
                    } else {
                        transform->position += glm::vec3(30.0f, 30.0f, 0.0f);
                    }
                    transform->world_position = target_world.world_position;
                }
            }

            if (snap.has_hierarchy) {
                const EntityID orig_parent = snap.hierarchy.parent;
                if (is_root) {
                    // Keep original parent link if root was parented to an unselected entity
                    snap.hierarchy.parent = orig_parent;
                } else {
                    // Remap internal parent to the newly cloned parent
                    snap.hierarchy.parent = old_to_new.contains(orig_parent) ? old_to_new[orig_parent] : 0;
                }

                std::vector<EntityID> remapped_children;
                for (EntityID orig_child : snap.hierarchy.children) {
                    if (old_to_new.contains(orig_child)) {
                        remapped_children.push_back(old_to_new[orig_child]);
                    }
                }
                snap.hierarchy.children = std::move(remapped_children);
            }

            attach_snapshot_components(*m_em, snap);
            m_cloned_snapshots.push_back(std::move(snap));
        }

        // 3. Establish hierarchy links using set_parent
        for (const auto& snap : m_cloned_snapshots) {
            if (snap.has_hierarchy && snap.hierarchy.parent != 0 && m_em->is_alive(snap.hierarchy.parent)) {
                const auto result = m_em->set_parent(snap.id, snap.hierarchy.parent);
                if (!result.succeeded()) {
                    ENGINE_LOG_ERROR("[COMMAND] Failed to parent duplicated entity #{} to #{}: {}", snap.id, snap.hierarchy.parent, result.message());
                    rollback_created(all_new_ids);
                    m_cloned_snapshots.clear();
                    m_new_root_ids.clear();
                    return false;
                }
            }
        }

        // 4. Finalize components
        for (const auto& snap : m_cloned_snapshots) {
            if (!finalize_snapshot_components(*m_em, snap.id)) {
                ENGINE_LOG_ERROR("[COMMAND] Failed to finalize duplicated entity #{}", snap.id);
                rollback_created(all_new_ids);
                m_cloned_snapshots.clear();
                m_new_root_ids.clear();
                return false;
            }
        }

        m_executed_once = true;
    } else {
        // Redo path: recreate all entities transactionally
        std::vector<EntityID> restored_entities;
        for (const auto& snap : m_cloned_snapshots) {
            if (!restore_entity_snapshot(*m_em, snap)) {
                ENGINE_LOG_ERROR("[COMMAND] Failed to restore entity snapshot #{} on redo", snap.id);
                rollback_created(restored_entities);
                return false;
            }
            restored_entities.push_back(snap.id);
        }
        for (const auto& snap : m_cloned_snapshots) {
            if (snap.has_hierarchy && snap.hierarchy.parent != 0 && m_em->is_alive(snap.hierarchy.parent)) {
                const auto result = m_em->set_parent(snap.id, snap.hierarchy.parent);
                if (!result.succeeded()) {
                    ENGINE_LOG_ERROR("[COMMAND] Failed to reparent entity #{} to #{} on redo: {}", snap.id, snap.hierarchy.parent, result.message());
                    rollback_created(restored_entities);
                    return false;
                }
            }
        }
        for (const auto& snap : m_cloned_snapshots) {
            if (!finalize_snapshot_components(*m_em, snap.id)) {
                ENGINE_LOG_ERROR("[COMMAND] Failed to finalize entity #{} on redo", snap.id);
                rollback_created(restored_entities);
                return false;
            }
        }
    }

    TransformSystem::update(*m_em);
    for (const auto& snap : m_cloned_snapshots) {
        if (m_em->has_component<TilemapComponent>(snap.id)) {
            TilemapColliderSystem::build_composite_colliders(*m_em, snap.id);
        }
        if (m_em->has_component<TransformComponent>(snap.id)) {
            PhysicsSystem::sync_body_hierarchy(*m_em, snap.id);
        }
    }

    if (m_selection) {
        std::unordered_set<EntityID> duplicated(m_new_root_ids.begin(), m_new_root_ids.end());
        m_selection->set_selection(duplicated);
    }
    return true;
}

bool DuplicateEntitiesCommand::undo() {
    if (!m_valid || !m_em) return false;

    // Destroy in reverse order (children before parents)
    for (auto it = m_cloned_snapshots.rbegin(); it != m_cloned_snapshots.rend(); ++it) {
        const EntityID id = it->id;
        if (m_em->is_alive(id)) {
            if (m_em->has_component<TilemapComponent>(id)) {
                TilemapColliderSystem::clear_composite_colliders(*m_em, id);
            }
            m_em->destroy_entity(id);
        }
    }

    TransformSystem::update(*m_em);

    if (m_selection) {
        std::unordered_set<EntityID> sources;
        for (EntityID id : m_source_entities) {
            if (m_em->is_alive(id)) sources.insert(id);
        }
        m_selection->set_selection(sources);
    }
    return true;
}

// ---------------------------------------------------------------------------
// 4. Paint Tiles Command
// ---------------------------------------------------------------------------
PaintTilesCommand::PaintTilesCommand(
    EntityManager* em,
    EntityID tilemap_entity,
    std::unordered_map<int64_t, uint16_t> old_tiles,
    std::unordered_map<int64_t, uint16_t> new_tiles,
    std::string description
) : m_em(em),
    m_tilemap_entity(tilemap_entity),
    m_old_tiles(std::move(old_tiles)),
    m_new_tiles(std::move(new_tiles)),
    m_description(std::move(description)) {}

bool PaintTilesCommand::execute() {
    if (!m_em || !m_em->is_alive(m_tilemap_entity) || !m_em->has_component<TilemapComponent>(m_tilemap_entity)) return false;

    auto& tilemap = m_em->get_component<TilemapComponent>(m_tilemap_entity);
    for (const auto& [key, new_id] : m_new_tiles) {
        int x = 0, y = 0;
        TilemapComponent::parse_key(key, x, y);
        tilemap.set_tile(x, y, new_id);
    }
    TilemapColliderSystem::build_composite_colliders(*m_em, m_tilemap_entity);
    return true;
}

bool PaintTilesCommand::undo() {
    if (!m_em || !m_em->is_alive(m_tilemap_entity) || !m_em->has_component<TilemapComponent>(m_tilemap_entity)) return false;

    auto& tilemap = m_em->get_component<TilemapComponent>(m_tilemap_entity);
    for (const auto& [key, old_id] : m_old_tiles) {
        int x = 0, y = 0;
        TilemapComponent::parse_key(key, x, y);
        tilemap.set_tile(x, y, old_id);
    }
    TilemapColliderSystem::build_composite_colliders(*m_em, m_tilemap_entity);
    return true;
}

} // namespace Engine
