#include "EntityCommands.hpp"
#include "../../physics/PhysicsSystem.hpp"

namespace Engine {

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

        EntitySnapshot snap;
        snap.id = id;
        if (m_em->has_component<TagComponent>(id)) snap.tag = m_em->get_component<TagComponent>(id);
        if (m_em->has_component<TransformComponent>(id)) snap.transform = m_em->get_component<TransformComponent>(id);
        if (m_em->has_component<SpriteComponent>(id)) {
            snap.has_sprite = true;
            snap.sprite = m_em->get_component<SpriteComponent>(id);
        }
        if (m_em->has_component<RigidBodyComponent>(id)) {
            snap.has_rigidbody = true;
            snap.rigidbody = m_em->get_component<RigidBodyComponent>(id);
        }
        if (m_em->has_component<ColliderComponent>(id)) {
            snap.has_collider = true;
            snap.collider = m_em->get_component<ColliderComponent>(id);
        }

        m_snapshots.push_back(snap);
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
        EntityID new_id = m_em->create_entity();
        m_em->add_component<TagComponent>(new_id, snap.tag);
        m_em->add_component<TransformComponent>(new_id, snap.transform);

        if (snap.has_sprite) m_em->add_component<SpriteComponent>(new_id, snap.sprite);
        if (snap.has_rigidbody) m_em->add_component<RigidBodyComponent>(new_id, snap.rigidbody);
        if (snap.has_collider) m_em->add_component<ColliderComponent>(new_id, snap.collider);

        restored_ids.insert(new_id);
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

    m_created_id = m_em->create_entity();
    m_em->add_component<TagComponent>(m_created_id, m_snapshot.tag);
    m_em->add_component<TransformComponent>(m_created_id, m_snapshot.transform);

    if (m_snapshot.has_sprite) m_em->add_component<SpriteComponent>(m_created_id, m_snapshot.sprite);
    if (m_snapshot.has_rigidbody) m_em->add_component<RigidBodyComponent>(m_created_id, m_snapshot.rigidbody);
    if (m_snapshot.has_collider) m_em->add_component<ColliderComponent>(m_created_id, m_snapshot.collider);

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

} // namespace Engine
