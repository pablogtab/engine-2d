#include "HierarchyCommands.hpp"
#include "../../ecs/TransformSystem.hpp"
#include "../../core/Log.hpp"

namespace Engine {

ReparentEntityCommand::ReparentEntityCommand(
    EntityManager* em,
    EntityID entity,
    EntityID new_parent,
    std::shared_ptr<EditorSelection> selection,
    std::string custom_description
) : m_em(em),
    m_selection(std::move(selection)),
    m_entity(entity),
    m_new_parent(new_parent) {
    if (!m_em || !m_em->is_alive(m_entity)) {
        m_valid = false;
        return;
    }

    if (!m_em->has_component<TransformComponent>(m_entity)) {
        m_valid = false;
        return;
    }

    if (m_new_parent != 0) {
        if (!m_em->is_alive(m_new_parent) || !m_em->has_component<TransformComponent>(m_new_parent) || m_entity == m_new_parent) {
            m_valid = false;
            return;
        }
    }

    if (m_em->has_component<HierarchyComponent>(m_entity)) {
        m_old_parent = m_em->get_component<HierarchyComponent>(m_entity).parent;
    } else {
        m_old_parent = 0;
    }

    auto local_transform_opt = TransformSystem::calculate_reparent_local_transform(*m_em, m_entity, m_new_parent);
    if (!local_transform_opt) {
        ENGINE_LOG_WARN(
            "[HIERARCHY] Cannot reparent entity #{} to parent #{}: world to local transform inversion failed (e.g. degenerate parent scale)",
            m_entity,
            m_new_parent
        );
        m_valid = false;
        return;
    }

    m_had_transform = true;
    m_old_transform = m_em->get_component<TransformComponent>(m_entity);
    m_new_transform = *local_transform_opt;

    if (!custom_description.empty()) {
        m_description = std::move(custom_description);
    } else {
        std::string entity_name = "Entity #" + std::to_string(m_entity);
        if (m_em->has_component<TagComponent>(m_entity)) {
            entity_name = "'" + m_em->get_component<TagComponent>(m_entity).tag + "'";
        }

        if (m_new_parent == 0) {
            m_description = "Unparent " + entity_name;
        } else {
            std::string parent_name = "Entity #" + std::to_string(m_new_parent);
            if (m_em->has_component<TagComponent>(m_new_parent)) {
                parent_name = "'" + m_em->get_component<TagComponent>(m_new_parent).tag + "'";
            }
            m_description = "Reparent " + entity_name + " to " + parent_name;
        }
    }

    m_valid = true;
}

bool ReparentEntityCommand::execute() {
    if (!m_valid || !m_em || !m_em->is_alive(m_entity)) return false;

    if (m_new_parent == 0) {
        const auto result = m_em->remove_parent(m_entity);
        if (!result.succeeded()) {
            ENGINE_LOG_WARN("[HIERARCHY] ReparentEntityCommand remove_parent failed: {}", result.message());
            return false;
        }
    } else {
        const auto result = m_em->set_parent(m_entity, m_new_parent);
        if (!result.succeeded()) {
            ENGINE_LOG_WARN("[HIERARCHY] ReparentEntityCommand set_parent failed: {}", result.message());
            return false;
        }
    }

    if (m_had_transform && m_em->has_component<TransformComponent>(m_entity)) {
        m_em->get_component<TransformComponent>(m_entity) = m_new_transform;
    }

    TransformSystem::update(*m_em);
    return true;
}

bool ReparentEntityCommand::undo() {
    if (!m_valid || !m_em || !m_em->is_alive(m_entity)) return false;

    if (m_old_parent == 0) {
        const auto result = m_em->remove_parent(m_entity);
        if (!result.succeeded()) {
            ENGINE_LOG_WARN("[HIERARCHY] ReparentEntityCommand undo remove_parent failed: {}", result.message());
            return false;
        }
    } else {
        const auto result = m_em->set_parent(m_entity, m_old_parent);
        if (!result.succeeded()) {
            ENGINE_LOG_WARN("[HIERARCHY] ReparentEntityCommand undo set_parent failed: {}", result.message());
            return false;
        }
    }

    if (m_had_transform && m_em->has_component<TransformComponent>(m_entity)) {
        m_em->get_component<TransformComponent>(m_entity) = m_old_transform;
    }

    TransformSystem::update(*m_em);
    return true;
}

} // namespace Engine
