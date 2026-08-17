#include "EntityCommands.hpp"
#include "../../physics/PhysicsSystem.hpp"
#include "../../tilemap/Tilemap.hpp"
#include "../../tilemap/TilemapColliderSystem.hpp"

namespace Engine {

EntitySnapshot capture_entity_snapshot(EntityManager& em, EntityID entity) {
    EntitySnapshot snapshot;
    snapshot.id = entity;
    if (!em.is_alive(entity)) return snapshot;

    if (em.has_component<TagComponent>(entity)) { snapshot.has_tag = true; snapshot.tag = em.get_component<TagComponent>(entity); }
    if (em.has_component<TransformComponent>(entity)) { snapshot.has_transform = true; snapshot.transform = em.get_component<TransformComponent>(entity); }
    if (em.has_component<HierarchyComponent>(entity)) { snapshot.has_hierarchy = true; snapshot.hierarchy = em.get_component<HierarchyComponent>(entity); }
    if (em.has_component<SpriteComponent>(entity)) { snapshot.has_sprite = true; snapshot.sprite = em.get_component<SpriteComponent>(entity); }
    if (em.has_component<RigidBodyComponent>(entity)) { snapshot.has_rigidbody = true; snapshot.rigidbody = em.get_component<RigidBodyComponent>(entity); }
    if (em.has_component<ColliderComponent>(entity)) { snapshot.has_collider = true; snapshot.collider = em.get_component<ColliderComponent>(entity); }
    if (em.has_component<TilemapComponent>(entity)) { snapshot.has_tilemap = true; snapshot.tilemap = em.get_component<TilemapComponent>(entity); }
    if (em.has_component<CameraComponent>(entity)) { snapshot.has_camera = true; snapshot.camera = em.get_component<CameraComponent>(entity); }
    if (em.has_component<AnimationComponent>(entity)) { snapshot.has_animation = true; snapshot.animation = em.get_component<AnimationComponent>(entity); }
    return snapshot;
}

static void attach_snapshot_components(EntityManager& em, const EntitySnapshot& snapshot) {
    if (snapshot.has_tag) em.add_component<TagComponent>(snapshot.id, snapshot.tag);
    if (snapshot.has_transform) em.add_component<TransformComponent>(snapshot.id, snapshot.transform);
    if (snapshot.has_sprite) em.add_component<SpriteComponent>(snapshot.id, snapshot.sprite);
    if (snapshot.has_rigidbody) em.add_component<RigidBodyComponent>(snapshot.id, snapshot.rigidbody);
    if (snapshot.has_collider) em.add_component<ColliderComponent>(snapshot.id, snapshot.collider);
    if (snapshot.has_tilemap) em.add_component<TilemapComponent>(snapshot.id, snapshot.tilemap);
    if (snapshot.has_camera) em.add_component<CameraComponent>(snapshot.id, snapshot.camera);
    if (snapshot.has_animation) em.add_component<AnimationComponent>(snapshot.id, snapshot.animation);
    if (snapshot.has_hierarchy) em.add_component<HierarchyComponent>(snapshot.id, HierarchyComponent{});
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

void ModifyEntityCommand::execute() {
    if (m_em && m_em->is_alive(m_entity) && m_apply_after) {
        m_apply_after(*m_em, m_entity);
    }
}

void ModifyEntityCommand::undo() {
    if (m_em && m_em->is_alive(m_entity) && m_apply_before) {
        m_apply_before(*m_em, m_entity);
    }
}

// ---------------------------------------------------------------------------
// 1. Move Entities Command
// ---------------------------------------------------------------------------
MoveEntitiesCommand::MoveEntitiesCommand(
    EntityManager* em,
    std::unordered_map<EntityID, glm::vec3> old_positions,
    std::unordered_map<EntityID, glm::vec3> new_positions,
    std::string description
) : m_em(em), m_old_positions(std::move(old_positions)), m_new_positions(std::move(new_positions)), m_description(std::move(description)) {}

void MoveEntitiesCommand::execute() {
    if (!m_em) return;

    for (const auto& [id, new_pos] : m_new_positions) {
        if (m_em->is_alive(id) && m_em->has_component<TransformComponent>(id)) {
            auto& tx = m_em->get_component<TransformComponent>(id);
            tx.position = new_pos;
            tx.world_position = new_pos;
            PhysicsSystem::set_position(*m_em, id, glm::vec2(new_pos.x, new_pos.y));
        }
    }
}

void MoveEntitiesCommand::undo() {
    if (!m_em) return;

    for (const auto& [id, old_pos] : m_old_positions) {
        if (m_em->is_alive(id) && m_em->has_component<TransformComponent>(id)) {
            auto& tx = m_em->get_component<TransformComponent>(id);
            tx.position = old_pos;
            tx.world_position = old_pos;
            PhysicsSystem::set_position(*m_em, id, glm::vec2(old_pos.x, old_pos.y));
        }
    }
}

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
        m_description = "Delete " + m_snapshots[0].tag.tag;
    } else {
        m_description = "Delete (" + std::to_string(m_snapshots.size()) + ") Entities";
    }
}

void DeleteEntitiesCommand::execute() {
    if (!m_em) return;

    for (const auto& snap : m_snapshots) {
        if (m_em->is_alive(snap.id)) {
            m_em->destroy_entity(snap.id);
        }
    }

    if (m_selection) {
        m_selection->clear();
    }
}

void DeleteEntitiesCommand::undo() {
    if (!m_em) return;

    std::unordered_set<EntityID> restored_ids;

    for (const auto& snap : m_snapshots) {
        if (restore_entity_snapshot(*m_em, snap)) restored_ids.insert(snap.id);
    }

    // Reconnect hierarchy only after every entity has been restored.
    for (const auto& snap : m_snapshots) {
        if (!snap.has_hierarchy || !m_em->is_alive(snap.id)) continue;
        if (snap.hierarchy.parent != 0 && m_em->is_alive(snap.hierarchy.parent)) {
            m_em->set_parent(snap.id, snap.hierarchy.parent);
        }
        for (EntityID child : snap.hierarchy.children) {
            if (m_em->is_alive(child)) m_em->set_parent(child, snap.id);
        }
    }

    if (m_selection) {
        m_selection->set_selection(restored_ids);
    }
}

// ---------------------------------------------------------------------------
// 3. Create Entity Command
// ---------------------------------------------------------------------------
CreateEntityCommand::CreateEntityCommand(
    EntityManager* em,
    std::shared_ptr<EditorSelection> selection,
    EntitySnapshot snapshot
) : m_em(em), m_selection(selection), m_snapshot(std::move(snapshot)) {
    m_description = "Create " + m_snapshot.tag.tag;
}

void CreateEntityCommand::execute() {
    if (!m_em) return;

    if (m_snapshot.id == 0) {
        m_snapshot.id = m_em->create_entity();
        attach_snapshot_components(*m_em, m_snapshot);
    } else if (!restore_entity_snapshot(*m_em, m_snapshot)) {
        return;
    }
    m_created_id = m_snapshot.id;

    if (m_snapshot.has_hierarchy && m_snapshot.hierarchy.parent != 0 && m_em->is_alive(m_snapshot.hierarchy.parent)) {
        m_em->set_parent(m_created_id, m_snapshot.hierarchy.parent);
    }

    if (m_selection) {
        m_selection->select(m_created_id, false);
    }
}

void CreateEntityCommand::undo() {
    if (!m_em) return;

    if (m_created_id != 0 && m_em->is_alive(m_created_id)) {
        m_em->destroy_entity(m_created_id);
    }

    if (m_selection && m_selection->is_selected(m_created_id)) {
        m_selection->clear();
    }
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

void PaintTilesCommand::execute() {
    if (!m_em || !m_em->is_alive(m_tilemap_entity) || !m_em->has_component<TilemapComponent>(m_tilemap_entity)) return;

    auto& tilemap = m_em->get_component<TilemapComponent>(m_tilemap_entity);
    for (const auto& [key, new_id] : m_new_tiles) {
        int x = 0, y = 0;
        TilemapComponent::parse_key(key, x, y);
        tilemap.set_tile(x, y, new_id);
    }
    TilemapColliderSystem::build_composite_colliders(*m_em, m_tilemap_entity);
}

void PaintTilesCommand::undo() {
    if (!m_em || !m_em->is_alive(m_tilemap_entity) || !m_em->has_component<TilemapComponent>(m_tilemap_entity)) return;

    auto& tilemap = m_em->get_component<TilemapComponent>(m_tilemap_entity);
    for (const auto& [key, old_id] : m_old_tiles) {
        int x = 0, y = 0;
        TilemapComponent::parse_key(key, x, y);
        tilemap.set_tile(x, y, old_id);
    }
    TilemapColliderSystem::build_composite_colliders(*m_em, m_tilemap_entity);
}

} // namespace Engine
