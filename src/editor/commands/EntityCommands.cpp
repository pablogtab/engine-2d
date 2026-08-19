#include "EntityCommands.hpp"
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
    std::string description
) : m_registry(registry),
    m_em(em),
    m_entity(entity),
    m_component_key(std::move(component_key)),
    m_before(std::move(before)),
    m_after(std::move(after)),
    m_serialization_context(serialization_context),
    m_description(std::move(description)) {}

bool SetRegisteredComponentCommand::execute() {
    return apply(m_after);
}

bool SetRegisteredComponentCommand::undo() {
    return apply(m_before);
}

bool SetRegisteredComponentCommand::apply(const ComponentSnapshotSet& snapshot) {
    if (!m_registry || !m_em || !m_em->is_alive(m_entity)) return false;
    const ComponentDescriptor* descriptor = m_registry->find_by_key(m_component_key);
    if (!descriptor) {
        ENGINE_LOG_ERROR("[EDITOR COMMAND] Unknown component '{}'.", m_component_key);
        return false;
    }
    descriptor->restore(*m_em, m_entity, snapshot);
    if (!descriptor->has(*m_em, m_entity) || !descriptor->finalize) return true;

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
    std::string description
) : m_em(em), m_before(std::move(before)), m_after(std::move(after)), m_description(std::move(description)) {}

bool TransformEntitiesCommand::apply(const std::unordered_map<EntityID, TransformComponent>& states) {
    if (!m_em) return false;
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

// ---------------------------------------------------------------------------
// 2. Delete Entities Command
// ---------------------------------------------------------------------------
DeleteEntitiesCommand::DeleteEntitiesCommand(
    EntityManager* em,
    std::shared_ptr<EditorSelection> selection,
    std::vector<EntityID> entities_to_delete
) : m_em(em), m_selection(selection) {
    if (!m_em) return;

    for (EntityID id : entities_to_delete) {
        if (!m_em->is_alive(id)) continue;

        m_snapshots.push_back(capture_entity_snapshot(*m_em, id));
    }

    if (m_snapshots.size() == 1) {
        m_description = "Delete " + snapshot_tag(m_snapshots[0]);
    } else {
        m_description = "Delete (" + std::to_string(m_snapshots.size()) + ") Entities";
    }
}

bool DeleteEntitiesCommand::execute() {
    if (!m_em) return false;

    for (const auto& snap : m_snapshots) {
        if (m_em->is_alive(snap.id)) {
            if (m_em->has_component<TilemapComponent>(snap.id)) {
                TilemapColliderSystem::clear_composite_colliders(*m_em, snap.id);
            }
            m_em->destroy_entity(snap.id);
        }
    }

    if (m_selection) {
        m_selection->clear();
    }
    return true;
}

bool DeleteEntitiesCommand::undo() {
    if (!m_em) return false;

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
    EntitySnapshot snapshot
) : m_em(em), m_selection(selection), m_snapshot(std::move(snapshot)) {
    m_description = "Create " + snapshot_tag(m_snapshot);
}

bool CreateEntityCommand::execute() {
    if (!m_em) return false;

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
DuplicateEntitiesCommand::DuplicateEntitiesCommand(
    EntityManager* em,
    std::shared_ptr<EditorSelection> selection,
    std::vector<EntityID> source_entities
) : m_em(em), m_selection(std::move(selection)), m_source_entities(std::move(source_entities)) {
    if (!m_em) return;

    for (EntityID source_id : m_source_entities) {
        if (!m_em->is_alive(source_id)) continue;

        EntitySnapshot snapshot = capture_entity_snapshot(*m_em, source_id);
        snapshot.id = 0;
        if (auto* tag = snapshot.registered_components.get<TagComponent>()) tag->tag += "_Copy";
        if (auto* transform = snapshot.registered_components.get<TransformComponent>()) {
            // Preserve the source's local coordinate space under a parent.
            transform->position += glm::vec3(30.0f, 30.0f, 0.0f);
            transform->world_position += glm::vec3(30.0f, 30.0f, 0.0f);
        }
        if (snapshot.has_hierarchy) snapshot.hierarchy.children.clear();
        m_create_commands.push_back(std::make_unique<CreateEntityCommand>(m_em, m_selection, std::move(snapshot)));
    }

    if (m_create_commands.size() == 1) m_description = "Duplicate Entity";
}

bool DuplicateEntitiesCommand::execute() {
    if (!m_em) return false;

    m_duplicate_entities.clear();
    for (auto& command : m_create_commands) {
        if (!command->execute()) continue;
        const EntityID id = command->get_created_entity();
        if (id != 0 && m_em->is_alive(id)) m_duplicate_entities.push_back(id);
    }

    TransformSystem::update(*m_em);
    for (EntityID id : m_duplicate_entities) {
        if (!m_em->has_component<TransformComponent>(id)) continue;
        PhysicsSystem::sync_body_hierarchy(*m_em, id);
    }

    if (m_selection) {
        std::unordered_set<EntityID> duplicated(m_duplicate_entities.begin(), m_duplicate_entities.end());
        m_selection->set_selection(duplicated);
    }
    return true;
}

bool DuplicateEntitiesCommand::undo() {
    if (!m_em) return false;
    for (auto it = m_create_commands.rbegin(); it != m_create_commands.rend(); ++it) (*it)->undo();

    if (m_selection) {
        std::unordered_set<EntityID> sources;
        for (EntityID id : m_source_entities) if (m_em->is_alive(id)) sources.insert(id);
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
