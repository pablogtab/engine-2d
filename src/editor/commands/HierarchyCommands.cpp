#include "HierarchyCommands.hpp"
#include "../state/EditorEntityStateStore.hpp"
#include "../tools/EditorTransformUtils.hpp"
#include "../../ecs/TransformSystem.hpp"
#include "../../physics/PhysicsSystem.hpp"
#include "../../core/Log.hpp"
#include <algorithm>
#include <unordered_set>

namespace Engine {

RemoveMissingHierarchyChildReferenceCommand::RemoveMissingHierarchyChildReferenceCommand(
    EntityManager* em,
    EntityID parent,
    EntityID missing_child
) : m_em(em),
    m_parent(parent) {
    if (!m_em || !m_em->is_alive(m_parent) ||
        !m_em->has_component<HierarchyComponent>(m_parent) ||
        m_em->is_alive(missing_child)) return;

    m_before = m_em->get_component<HierarchyComponent>(m_parent).children;
    m_after = m_before;
    std::erase(m_after, missing_child);
    m_valid = m_before != m_after;
}

bool RemoveMissingHierarchyChildReferenceCommand::execute() {
    if (!m_valid || !m_em || !m_em->is_alive(m_parent) ||
        !m_em->has_component<HierarchyComponent>(m_parent)) return false;
    m_em->get_component<HierarchyComponent>(m_parent).children = m_after;
    return true;
}

bool RemoveMissingHierarchyChildReferenceCommand::undo() {
    if (!m_valid || !m_em || !m_em->is_alive(m_parent) ||
        !m_em->has_component<HierarchyComponent>(m_parent)) return false;
    m_em->get_component<HierarchyComponent>(m_parent).children = m_before;
    return true;
}

// ---------------------------------------------------------------------------
// 1. ReparentEntityCommand
// ---------------------------------------------------------------------------
ReparentEntityCommand::ReparentEntityCommand(
    EntityManager* em,
    EntityID entity,
    EntityID new_parent,
    std::shared_ptr<EditorSelection> selection,
    std::string custom_description,
    const EditorEntityStateStore* state_store,
    std::optional<size_t> target_index
) : m_em(em),
    m_selection(std::move(selection)),
    m_entity(entity),
    m_new_parent(new_parent),
    m_target_index(target_index) {
    if (!m_em || !m_em->is_alive(m_entity)) {
        m_valid = false;
        return;
    }

    if (state_store && state_store->is_locked(m_entity)) {
        ENGINE_LOG_WARN("[HIERARCHY] Cannot reparent locked entity #{}", m_entity);
        m_valid = false;
        return;
    }

    if (m_new_parent != 0 && state_store && state_store->is_locked(m_new_parent)) {
        ENGINE_LOG_WARN("[HIERARCHY] Cannot reparent entity #{} to locked parent #{}", m_entity, m_new_parent);
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

    m_old_index = m_em->get_sibling_index(m_entity);
    if (m_em->has_component<HierarchyComponent>(m_entity)) {
        m_old_parent = m_em->get_component<HierarchyComponent>(m_entity).parent;
    } else {
        m_old_parent = 0;
    }

    if (m_old_parent != 0 && state_store && state_store->is_locked(m_old_parent)) {
        ENGINE_LOG_WARN("[HIERARCHY] Cannot reparent entity #{} from locked parent #{}", m_entity, m_old_parent);
        m_valid = false;
        return;
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
        const auto result = m_em->remove_parent(m_entity, m_target_index);
        if (!result.succeeded()) {
            ENGINE_LOG_WARN("[HIERARCHY] ReparentEntityCommand remove_parent failed: {}", result.message());
            return false;
        }
    } else {
        const auto result = m_em->set_parent(m_entity, m_new_parent, m_target_index);
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
        const auto result = m_em->remove_parent(m_entity, m_old_index);
        if (!result.succeeded()) {
            ENGINE_LOG_WARN("[HIERARCHY] ReparentEntityCommand undo remove_parent failed: {}", result.message());
            return false;
        }
    } else {
        const auto result = m_em->set_parent(m_entity, m_old_parent, m_old_index);
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

// ---------------------------------------------------------------------------
// 1a. ReparentEntitiesCommand (Bulk / Multi-Entity Reparenting)
// ---------------------------------------------------------------------------
static bool is_entity_descendant(const EntityManager& em, EntityID potential_descendant, EntityID ancestor) {
    if (potential_descendant == 0 || ancestor == 0) return false;
    if (potential_descendant == ancestor) return true;
    if (!em.has_component<HierarchyComponent>(potential_descendant)) return false;

    EntityID curr = em.get_component<HierarchyComponent>(potential_descendant).parent;
    while (curr != 0 && em.is_alive(curr)) {
        if (curr == ancestor) return true;
        if (!em.has_component<HierarchyComponent>(curr)) break;
        curr = em.get_component<HierarchyComponent>(curr).parent;
    }
    return false;
}

ReparentEntitiesCommand::ReparentEntitiesCommand(
    EntityManager* em,
    std::vector<EntityID> entities,
    EntityID new_parent,
    std::shared_ptr<EditorSelection> selection,
    std::string custom_description,
    const EditorEntityStateStore* state_store,
    std::optional<size_t> target_index
) : m_em(em),
    m_selection(std::move(selection)),
    m_original_selection(entities),
    m_new_parent(new_parent),
    m_target_index(target_index) {
    if (!m_em || entities.empty()) {
        m_valid = false;
        return;
    }

    TransformSystem::update(*m_em);

    const std::unordered_set<EntityID> entity_set(entities.begin(), entities.end());

    // Filter to roots among the selected entities (exclude children whose ancestor is also selected)
    std::vector<EntityID> filtered_roots;
    for (EntityID id : entities) {
        if (!m_em->is_alive(id) || !m_em->has_component<TransformComponent>(id)) continue;
        if (!has_selected_ancestor(*m_em, id, entity_set)) {
            filtered_roots.push_back(id);
        }
    }

    if (filtered_roots.empty()) {
        m_valid = false;
        return;
    }

    // Validate new parent
    if (m_new_parent != 0) {
        if (!m_em->is_alive(m_new_parent) || !m_em->has_component<TransformComponent>(m_new_parent)) {
            m_valid = false;
            return;
        }

        if (state_store && state_store->is_locked(m_new_parent)) {
            ENGINE_LOG_WARN("[HIERARCHY] Cannot reparent entities to locked parent #{}", m_new_parent);
            m_valid = false;
            return;
        }

        // Cycle check: none of the filtered roots can be the new parent or an ancestor of the new parent
        for (EntityID root_id : filtered_roots) {
            if (root_id == m_new_parent || is_entity_descendant(*m_em, m_new_parent, root_id)) {
                ENGINE_LOG_WARN("[HIERARCHY] Cannot reparent entity #{} into its own descendant #{}", root_id, m_new_parent);
                m_valid = false;
                return;
            }
        }
    }

    // Validate each entity and pre-calculate local transforms
    for (size_t i = 0; i < filtered_roots.size(); ++i) {
        const EntityID id = filtered_roots[i];

        if (state_store && state_store->is_locked(id)) {
            ENGINE_LOG_WARN("[HIERARCHY] Cannot reparent locked entity #{}", id);
            m_valid = false;
            return;
        }

        EntityID old_parent = 0;
        if (m_em->has_component<HierarchyComponent>(id)) {
            old_parent = m_em->get_component<HierarchyComponent>(id).parent;
        }

        if (old_parent != 0 && state_store && state_store->is_locked(old_parent)) {
            ENGINE_LOG_WARN("[HIERARCHY] Cannot reparent entity #{} from locked parent #{}", id, old_parent);
            m_valid = false;
            return;
        }

        auto local_opt = TransformSystem::calculate_reparent_local_transform(*m_em, id, m_new_parent);
        if (!local_opt) {
            ENGINE_LOG_WARN("[HIERARCHY] Transform calculation failed for entity #{} reparented to parent #{}", id, m_new_parent);
            m_valid = false;
            return;
        }

        ReparentBatchEntry entry;
        entry.entity = id;
        entry.old_parent = old_parent;
        entry.old_index = m_em->get_sibling_index(id);
        entry.target_index = m_target_index.has_value() ? std::make_optional(*m_target_index + i) : std::nullopt;
        entry.old_transform = m_em->get_component<TransformComponent>(id);
        entry.new_transform = *local_opt;

        m_entries.push_back(std::move(entry));
    }

    if (m_entries.empty()) {
        m_valid = false;
        return;
    }

    if (!custom_description.empty()) {
        m_description = std::move(custom_description);
    } else if (m_entries.size() == 1) {
        std::string entity_name = "Entity #" + std::to_string(m_entries[0].entity);
        if (m_em->has_component<TagComponent>(m_entries[0].entity)) {
            entity_name = "'" + m_em->get_component<TagComponent>(m_entries[0].entity).tag + "'";
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
    } else {
        if (m_new_parent == 0) {
            m_description = "Unparent (" + std::to_string(m_entries.size()) + ") Entities";
        } else {
            std::string parent_name = "Entity #" + std::to_string(m_new_parent);
            if (m_em->has_component<TagComponent>(m_new_parent)) {
                parent_name = "'" + m_em->get_component<TagComponent>(m_new_parent).tag + "'";
            }
            m_description = "Reparent (" + std::to_string(m_entries.size()) + ") Entities to " + parent_name;
        }
    }

    m_valid = true;
}

bool ReparentEntitiesCommand::execute() {
    if (!m_valid || !m_em) return false;

    size_t applied_count = 0;
    for (size_t i = 0; i < m_entries.size(); ++i) {
        const auto& entry = m_entries[i];
        if (!m_em->is_alive(entry.entity)) {
            // Rollback previously applied entries
            for (size_t r = applied_count; r > 0; --r) {
                const auto& undo_entry = m_entries[r - 1];
                if (undo_entry.old_parent == 0) {
                    (void)m_em->remove_parent(undo_entry.entity, undo_entry.old_index);
                } else {
                    (void)m_em->set_parent(undo_entry.entity, undo_entry.old_parent, undo_entry.old_index);
                }
                if (m_em->has_component<TransformComponent>(undo_entry.entity)) {
                    m_em->get_component<TransformComponent>(undo_entry.entity) = undo_entry.old_transform;
                }
            }
            TransformSystem::update(*m_em);
            return false;
        }

        if (m_new_parent == 0) {
            const auto result = m_em->remove_parent(entry.entity, entry.target_index);
            if (!result.succeeded()) {
                ENGINE_LOG_WARN("[HIERARCHY] ReparentEntitiesCommand remove_parent failed on entity #{}: {}", entry.entity, result.message());
                // Rollback
                for (size_t r = applied_count; r > 0; --r) {
                    const auto& undo_entry = m_entries[r - 1];
                    if (undo_entry.old_parent == 0) {
                        (void)m_em->remove_parent(undo_entry.entity, undo_entry.old_index);
                    } else {
                        (void)m_em->set_parent(undo_entry.entity, undo_entry.old_parent, undo_entry.old_index);
                    }
                    if (m_em->has_component<TransformComponent>(undo_entry.entity)) {
                        m_em->get_component<TransformComponent>(undo_entry.entity) = undo_entry.old_transform;
                    }
                }
                TransformSystem::update(*m_em);
                return false;
            }
        } else {
            const auto result = m_em->set_parent(entry.entity, m_new_parent, entry.target_index);
            if (!result.succeeded()) {
                ENGINE_LOG_WARN("[HIERARCHY] ReparentEntitiesCommand set_parent failed on entity #{}: {}", entry.entity, result.message());
                // Rollback
                for (size_t r = applied_count; r > 0; --r) {
                    const auto& undo_entry = m_entries[r - 1];
                    if (undo_entry.old_parent == 0) {
                        (void)m_em->remove_parent(undo_entry.entity, undo_entry.old_index);
                    } else {
                        (void)m_em->set_parent(undo_entry.entity, undo_entry.old_parent, undo_entry.old_index);
                    }
                    if (m_em->has_component<TransformComponent>(undo_entry.entity)) {
                        m_em->get_component<TransformComponent>(undo_entry.entity) = undo_entry.old_transform;
                    }
                }
                TransformSystem::update(*m_em);
                return false;
            }
        }

        if (m_em->has_component<TransformComponent>(entry.entity)) {
            m_em->get_component<TransformComponent>(entry.entity) = entry.new_transform;
        }
        PhysicsSystem::sync_body_hierarchy(*m_em, entry.entity);
        applied_count++;
    }

    TransformSystem::update(*m_em);

    if (m_selection) {
        std::unordered_set<EntityID> reparented_set;
        for (const auto& entry : m_entries) {
            reparented_set.insert(entry.entity);
        }
        m_selection->set_selection(reparented_set);
    }
    return true;
}

bool ReparentEntitiesCommand::undo() {
    if (!m_valid || !m_em) return false;

    // Restore in reverse order
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
        if (!m_em->is_alive(it->entity)) continue;

        if (it->old_parent == 0) {
            const auto result = m_em->remove_parent(it->entity, it->old_index);
            if (!result.succeeded()) {
                ENGINE_LOG_WARN("[HIERARCHY] ReparentEntitiesCommand undo remove_parent failed: {}", result.message());
            }
        } else {
            const auto result = m_em->set_parent(it->entity, it->old_parent, it->old_index);
            if (!result.succeeded()) {
                ENGINE_LOG_WARN("[HIERARCHY] ReparentEntitiesCommand undo set_parent failed: {}", result.message());
            }
        }

        if (m_em->has_component<TransformComponent>(it->entity)) {
            m_em->get_component<TransformComponent>(it->entity) = it->old_transform;
        }
        PhysicsSystem::sync_body_hierarchy(*m_em, it->entity);
    }

    TransformSystem::update(*m_em);

    if (m_selection) {
        std::unordered_set<EntityID> original_set;
        for (EntityID id : m_original_selection) {
            if (m_em->is_alive(id)) original_set.insert(id);
        }
        m_selection->set_selection(original_set);
    }
    return true;
}

// ---------------------------------------------------------------------------
// 1b. ReorderEntityCommand
// ---------------------------------------------------------------------------
ReorderEntityCommand::ReorderEntityCommand(
    EntityManager* em,
    EntityID entity,
    size_t new_index,
    std::shared_ptr<EditorSelection> selection,
    const EditorEntityStateStore* state_store
) : m_em(em),
    m_selection(std::move(selection)),
    m_entity(entity),
    m_new_index(new_index) {
    if (!m_em || !m_em->is_alive(m_entity)) {
        m_valid = false;
        return;
    }

    if (state_store && state_store->is_locked(m_entity)) {
        ENGINE_LOG_WARN("[HIERARCHY] Cannot reorder locked entity #{}", m_entity);
        m_valid = false;
        return;
    }

    if (m_em->has_component<HierarchyComponent>(m_entity)) {
        m_parent = m_em->get_component<HierarchyComponent>(m_entity).parent;
    } else {
        m_parent = 0;
    }

    if (m_parent != 0 && state_store && state_store->is_locked(m_parent)) {
        ENGINE_LOG_WARN("[HIERARCHY] Cannot reorder child #{} under locked parent #{}", m_entity, m_parent);
        m_valid = false;
        return;
    }

    m_old_index = m_em->get_sibling_index(m_entity);
    size_t count = m_em->get_sibling_count(m_entity);
    if (m_new_index >= count && count > 0) {
        m_new_index = count - 1;
    }

    if (m_old_index == m_new_index) {
        m_valid = false;
        return;
    }

    std::string tag = "Entity #" + std::to_string(m_entity);
    if (m_em->has_component<TagComponent>(m_entity)) {
        tag = "'" + m_em->get_component<TagComponent>(m_entity).tag + "'";
    }
    m_description = "Reorder " + tag;
    m_valid = true;
}

bool ReorderEntityCommand::execute() {
    if (!m_valid || !m_em || !m_em->is_alive(m_entity)) return false;

    EntityID current_parent = 0;
    if (m_em->has_component<HierarchyComponent>(m_entity)) {
        current_parent = m_em->get_component<HierarchyComponent>(m_entity).parent;
    }
    if (current_parent != m_parent) {
        ENGINE_LOG_WARN("[HIERARCHY] ReorderEntityCommand execute rejected: entity parent changed from #{} to #{}", m_parent, current_parent);
        return false;
    }

    return m_em->reorder_sibling(m_entity, m_new_index);
}

bool ReorderEntityCommand::undo() {
    if (!m_valid || !m_em || !m_em->is_alive(m_entity)) return false;

    EntityID current_parent = 0;
    if (m_em->has_component<HierarchyComponent>(m_entity)) {
        current_parent = m_em->get_component<HierarchyComponent>(m_entity).parent;
    }
    if (current_parent != m_parent) {
        ENGINE_LOG_WARN("[HIERARCHY] ReorderEntityCommand undo rejected: entity parent changed from #{} to #{}", m_parent, current_parent);
        return false;
    }

    return m_em->reorder_sibling(m_entity, m_old_index);
}

// ---------------------------------------------------------------------------
// 2. GroupSelectedEntitiesCommand
// ---------------------------------------------------------------------------
GroupSelectedEntitiesCommand::GroupSelectedEntitiesCommand(
    EntityManager* em,
    std::shared_ptr<EditorSelection> selection,
    const std::vector<EntityID>& selected_entities,
    std::string group_name,
    const EditorEntityStateStore* state_store
) : m_em(em),
    m_selection(std::move(selection)),
    m_original_selection(selected_entities),
    m_group_name(std::move(group_name)) {
    if (!m_em || selected_entities.empty()) {
        m_valid = false;
        return;
    }

    // 1. Ensure world transforms are updated before computing centroids and spaces
    TransformSystem::update(*m_em);

    // 2. Filter out descendants: if parent and child are both selected, only keep the parent
    std::vector<EntityID> root_candidates;
    for (EntityID id : selected_entities) {
        if (!m_em->is_alive(id) || !m_em->has_component<TransformComponent>(id)) continue;

        bool has_selected_ancestor = false;
        EntityID curr = m_em->has_component<HierarchyComponent>(id)
            ? m_em->get_component<HierarchyComponent>(id).parent : 0;
        while (curr != 0 && m_em->is_alive(curr)) {
            if (std::find(selected_entities.begin(), selected_entities.end(), curr) != selected_entities.end()) {
                has_selected_ancestor = true;
                break;
            }
            curr = m_em->has_component<HierarchyComponent>(curr)
                ? m_em->get_component<HierarchyComponent>(curr).parent : 0;
        }
        if (!has_selected_ancestor && std::find(root_candidates.begin(), root_candidates.end(), id) == root_candidates.end()) {
            root_candidates.push_back(id);
        }
    }

    if (root_candidates.empty()) {
        m_valid = false;
        return;
    }

    if (state_store) {
        for (EntityID id : selected_entities) {
            if (state_store->is_locked(id)) {
                ENGINE_LOG_WARN("[HIERARCHY] Cannot group locked entity #{}", id);
                m_valid = false;
                return;
            }
        }
    }

    // 3. Determine Group Parent: if all candidates share the same parent, group is created under that parent
    EntityID common_parent = m_em->has_component<HierarchyComponent>(root_candidates[0])
        ? m_em->get_component<HierarchyComponent>(root_candidates[0]).parent : 0;
    bool all_same_parent = true;
    for (size_t i = 1; i < root_candidates.size(); ++i) {
        EntityID p = m_em->has_component<HierarchyComponent>(root_candidates[i])
            ? m_em->get_component<HierarchyComponent>(root_candidates[i]).parent : 0;
        if (p != common_parent) {
            all_same_parent = false;
            break;
        }
    }

    m_group_parent = all_same_parent ? common_parent : 0;
    if (m_group_parent != 0) {
        if (!m_em->is_alive(m_group_parent) || !m_em->has_component<TransformComponent>(m_group_parent)) {
            m_group_parent = 0;
        } else if (state_store && state_store->is_locked(m_group_parent)) {
            ENGINE_LOG_WARN("[HIERARCHY] Cannot group entities under locked parent #{}", m_group_parent);
            m_valid = false;
            return;
        }
    }

    // 4. Calculate World Centroid
    glm::vec3 centroid{0.0f};
    for (EntityID id : root_candidates) {
        centroid += m_em->get_component<TransformComponent>(id).world_position;
    }
    centroid /= static_cast<float>(root_candidates.size());

    // 5. Preflight group local transform under m_group_parent
    const TransformComponent* parent_world = (m_group_parent != 0 && m_em->has_component<TransformComponent>(m_group_parent))
        ? &m_em->get_component<TransformComponent>(m_group_parent) : nullptr;

    TransformComponent mock_group_world{centroid};
    auto group_local_opt = TransformSystem::world_to_local_transform(mock_group_world, parent_world);
    if (!group_local_opt) {
        ENGINE_LOG_WARN("[HIERARCHY] GroupSelectedEntitiesCommand failed: degenerate parent scale on parent #{}", m_group_parent);
        m_valid = false;
        return;
    }
    m_group_transform = *group_local_opt;

    // Computed effective group world transform for child conversion
    TransformComponent effective_group_world = TransformSystem::local_to_world_transform(m_group_transform, parent_world);

    // 6. Preflight child reparenting transforms relative to the new group
    m_children.clear();
    for (EntityID id : root_candidates) {
        const auto& child_world = m_em->get_component<TransformComponent>(id);
        auto child_local_opt = TransformSystem::world_to_local_transform(child_world, &effective_group_world);
        if (!child_local_opt) {
            ENGINE_LOG_WARN("[HIERARCHY] GroupSelectedEntitiesCommand failed: cannot compute local transform for entity #{}", id);
            m_valid = false;
            return;
        }

        m_children.push_back(GroupChildEntry{
            .entity = id,
            .old_parent = m_em->has_component<HierarchyComponent>(id) ? m_em->get_component<HierarchyComponent>(id).parent : 0,
            .old_local_transform = m_em->get_component<TransformComponent>(id),
            .new_local_transform = *child_local_opt
        });
    }

    m_description = "Create Group (" + std::to_string(m_children.size()) + " Entities)";
    m_valid = true;
}

bool GroupSelectedEntitiesCommand::execute() {
    if (!m_valid || !m_em) return false;

    // Stable ID preservation: create new ID on first execution, or recreate with existing ID on redo
    const bool was_new_entity = (m_group_id == 0);
    if (was_new_entity) {
        m_group_id = m_em->create_entity();
    } else {
        if (m_em->create_entity_with_id(m_group_id) == 0) {
            ENGINE_LOG_ERROR("[HIERARCHY] GroupSelectedEntitiesCommand failed recreating group entity with ID {}", m_group_id);
            return false;
        }
    }

    m_em->add_component<TagComponent>(m_group_id, TagComponent{m_group_name});
    m_em->add_component<TransformComponent>(m_group_id, m_group_transform);
    m_em->add_component<GroupComponent>(m_group_id, GroupComponent{});

    if (m_group_parent != 0) {
        const auto res = m_em->set_parent(m_group_id, m_group_parent);
        if (!res.succeeded()) {
            ENGINE_LOG_ERROR("[HIERARCHY] GroupSelectedEntitiesCommand failed setting parent: {}", res.message());
            m_em->destroy_entity(m_group_id);
            if (was_new_entity) m_group_id = 0;
            return false;
        }
    }

    size_t reparented_count = 0;
    bool success = true;
    for (size_t i = 0; i < m_children.size(); ++i) {
        const auto& child = m_children[i];
        if (!m_em->is_alive(child.entity)) {
            success = false;
            break;
        }
        const auto res = m_em->set_parent(child.entity, m_group_id);
        if (!res.succeeded()) {
            ENGINE_LOG_ERROR("[HIERARCHY] GroupSelectedEntitiesCommand failed reparenting child #{}: {}", child.entity, res.message());
            success = false;
            break;
        }
        if (m_em->has_component<TransformComponent>(child.entity)) {
            m_em->get_component<TransformComponent>(child.entity) = child.new_local_transform;
        }
        reparented_count++;
    }

    if (!success) {
        // Rollback already reparented children
        for (size_t i = 0; i < reparented_count; ++i) {
            const auto& child = m_children[i];
            if (child.old_parent == 0) {
                static_cast<void>(m_em->remove_parent(child.entity));
            } else {
                static_cast<void>(m_em->set_parent(child.entity, child.old_parent));
            }
            if (m_em->has_component<TransformComponent>(child.entity)) {
                m_em->get_component<TransformComponent>(child.entity) = child.old_local_transform;
            }
        }
        m_em->destroy_entity(m_group_id);
        if (was_new_entity) m_group_id = 0;
        TransformSystem::update(*m_em);
        return false;
    }

    TransformSystem::update(*m_em);

    if (m_selection) {
        m_selection->select(m_group_id, false);
    }
    return true;
}

bool GroupSelectedEntitiesCommand::undo() {
    if (!m_valid || !m_em || m_group_id == 0 || !m_em->is_alive(m_group_id)) return false;

    size_t restored_count = 0;
    bool success = true;
    for (size_t i = 0; i < m_children.size(); ++i) {
        const auto& child = m_children[i];
        if (!m_em->is_alive(child.entity)) {
            success = false;
            break;
        }
        HierarchyOperationResult res;
        if (child.old_parent == 0) {
            res = m_em->remove_parent(child.entity);
        } else {
            res = m_em->set_parent(child.entity, child.old_parent);
        }
        if (!res.succeeded()) {
            ENGINE_LOG_ERROR("[HIERARCHY] GroupSelectedEntitiesCommand undo failed reparenting child #{}: {}", child.entity, res.message());
            success = false;
            break;
        }
        if (m_em->has_component<TransformComponent>(child.entity)) {
            m_em->get_component<TransformComponent>(child.entity) = child.old_local_transform;
        }
        restored_count++;
    }

    if (!success) {
        // Rollback restoration: reconnect children to group
        for (size_t i = 0; i < restored_count; ++i) {
            const auto& child = m_children[i];
            static_cast<void>(m_em->set_parent(child.entity, m_group_id));
            if (m_em->has_component<TransformComponent>(child.entity)) {
                m_em->get_component<TransformComponent>(child.entity) = child.new_local_transform;
            }
        }
        TransformSystem::update(*m_em);
        return false;
    }

    m_em->destroy_entity(m_group_id);
    TransformSystem::update(*m_em);

    if (m_selection) {
        std::unordered_set<EntityID> sel;
        for (EntityID id : m_original_selection) {
            if (m_em->is_alive(id)) {
                sel.insert(id);
            }
        }
        m_selection->set_selection(sel);
    }
    return true;
}

// ---------------------------------------------------------------------------
// 3. UngroupEntitiesCommand
// ---------------------------------------------------------------------------
UngroupEntitiesCommand::UngroupEntitiesCommand(
    EntityManager* em,
    std::shared_ptr<EditorSelection> selection,
    const std::vector<EntityID>& target_entities,
    const EditorEntityStateStore* state_store
) : m_em(em),
    m_selection(std::move(selection)),
    m_original_selection(target_entities) {
    if (!m_em || target_entities.empty()) {
        m_valid = false;
        return;
    }

    TransformSystem::update(*m_em);

    // 1. Filter: keep only entities that have GroupComponent and are roots among selected groups
    std::vector<EntityID> root_groups;
    for (EntityID id : target_entities) {
        if (!m_em->is_alive(id) || !m_em->has_component<GroupComponent>(id) || !m_em->has_component<TransformComponent>(id)) {
            continue;
        }

        bool has_selected_group_ancestor = false;
        EntityID curr = m_em->has_component<HierarchyComponent>(id)
            ? m_em->get_component<HierarchyComponent>(id).parent : 0;
        while (curr != 0 && m_em->is_alive(curr)) {
            if (std::find(target_entities.begin(), target_entities.end(), curr) != target_entities.end() &&
                m_em->has_component<GroupComponent>(curr)) {
                has_selected_group_ancestor = true;
                break;
            }
            curr = m_em->has_component<HierarchyComponent>(curr)
                ? m_em->get_component<HierarchyComponent>(curr).parent : 0;
        }

        if (!has_selected_group_ancestor && std::find(root_groups.begin(), root_groups.end(), id) == root_groups.end()) {
            root_groups.push_back(id);
        }
    }

    if (root_groups.empty()) {
        m_valid = false;
        return;
    }

    if (state_store) {
        for (EntityID id : root_groups) {
            if (state_store->is_locked(id)) {
                ENGINE_LOG_WARN("[HIERARCHY] Cannot ungroup locked group #{}", id);
                m_valid = false;
                return;
            }
        }
    }

    // 2. Preflight each group and its children
    m_groups.clear();
    m_unpacked_children.clear();

    for (EntityID group_id : root_groups) {
        EntityID group_parent = m_em->has_component<HierarchyComponent>(group_id)
            ? m_em->get_component<HierarchyComponent>(group_id).parent : 0;

        if (group_parent != 0 && state_store && state_store->is_locked(group_parent)) {
            ENGINE_LOG_WARN("[HIERARCHY] Cannot ungroup group #{} into locked parent #{}", group_id, group_parent);
            m_valid = false;
            return;
        }

        const TransformComponent* parent_world = (group_parent != 0 && m_em->has_component<TransformComponent>(group_parent))
            ? &m_em->get_component<TransformComponent>(group_parent) : nullptr;

        std::vector<EntityID> direct_children;
        if (m_em->has_component<HierarchyComponent>(group_id)) {
            direct_children = m_em->get_component<HierarchyComponent>(group_id).children;
        }

        std::vector<UngroupChildEntry> child_entries;
        for (EntityID child : direct_children) {
            if (!m_em->is_alive(child) || !m_em->has_component<TransformComponent>(child)) continue;

            if (state_store && state_store->is_locked(child)) {
                ENGINE_LOG_WARN("[HIERARCHY] Cannot ungroup group #{} because child #{} is locked", group_id, child);
                m_valid = false;
                return;
            }

            const auto& child_world = m_em->get_component<TransformComponent>(child);
            auto new_local_opt = TransformSystem::world_to_local_transform(child_world, parent_world);
            if (!new_local_opt) {
                ENGINE_LOG_WARN("[HIERARCHY] UngroupEntitiesCommand failed: cannot compute transform for child #{} under parent #{}", child, group_parent);
                m_valid = false;
                return;
            }

            child_entries.push_back(UngroupChildEntry{
                .entity = child,
                .old_local_transform = m_em->get_component<TransformComponent>(child),
                .new_local_transform = *new_local_opt
            });
            m_unpacked_children.push_back(child);
        }

        m_groups.push_back(UngroupEntry{
            .group_id = group_id,
            .group_parent = group_parent,
            .group_snapshot = capture_entity_snapshot(*m_em, group_id),
            .children = std::move(child_entries)
        });
    }

    m_description = "Ungroup (" + std::to_string(m_groups.size()) + ") Groups";
    m_valid = true;
}

bool UngroupEntitiesCommand::execute() {
    if (!m_valid || !m_em) return false;

    // Step 1: Reparent children to group parents with transaction rollback if failure occurs
    struct ReparentRecord {
        EntityID child{0};
        EntityID group_id{0};
        TransformComponent old_local;
    };
    std::vector<ReparentRecord> moved_records;
    bool success = true;

    for (const auto& group : m_groups) {
        for (const auto& child : group.children) {
            if (!m_em->is_alive(child.entity)) {
                success = false;
                break;
            }
            HierarchyOperationResult res;
            if (group.group_parent == 0) {
                res = m_em->remove_parent(child.entity);
            } else {
                res = m_em->set_parent(child.entity, group.group_parent);
            }
            if (!res.succeeded()) {
                ENGINE_LOG_ERROR("[HIERARCHY] UngroupEntitiesCommand execute failed reparenting child #{}: {}", child.entity, res.message());
                success = false;
                break;
            }
            if (m_em->has_component<TransformComponent>(child.entity)) {
                m_em->get_component<TransformComponent>(child.entity) = child.new_local_transform;
            }
            moved_records.push_back({child.entity, group.group_id, child.old_local_transform});
        }
        if (!success) break;
    }

    if (!success) {
        // Rollback moved children back to their original groups
        for (const auto& rec : moved_records) {
            static_cast<void>(m_em->set_parent(rec.child, rec.group_id));
            if (m_em->has_component<TransformComponent>(rec.child)) {
                m_em->get_component<TransformComponent>(rec.child) = rec.old_local;
            }
        }
        TransformSystem::update(*m_em);
        return false;
    }

    // Step 2: Now that all children are safely reparented, destroy the group containers
    for (const auto& group : m_groups) {
        if (m_em->is_alive(group.group_id)) {
            m_em->destroy_entity(group.group_id);
        }
    }

    TransformSystem::update(*m_em);

    if (m_selection) {
        std::unordered_set<EntityID> sel;
        for (EntityID id : m_unpacked_children) {
            if (m_em->is_alive(id)) {
                sel.insert(id);
            }
        }
        m_selection->set_selection(sel);
    }
    return true;
}

bool UngroupEntitiesCommand::undo() {
    if (!m_valid || !m_em) return false;

    // Step 1: Restore all group entities
    std::vector<EntityID> restored_group_ids;
    bool restore_success = true;

    for (const auto& group : m_groups) {
        if (!restore_entity_snapshot(*m_em, group.group_snapshot)) {
            ENGINE_LOG_ERROR("[HIERARCHY] UngroupEntitiesCommand undo failed restoring group snapshot for ID {}", group.group_id);
            restore_success = false;
            break;
        }
        restored_group_ids.push_back(group.group_id);

        if (group.group_parent != 0) {
            if (!m_em->is_alive(group.group_parent)) {
                ENGINE_LOG_ERROR("[HIERARCHY] UngroupEntitiesCommand undo failed: parent #{} is no longer alive", group.group_parent);
                restore_success = false;
                break;
            }
            const auto res = m_em->set_parent(group.group_id, group.group_parent);
            if (!res.succeeded()) {
                ENGINE_LOG_ERROR("[HIERARCHY] UngroupEntitiesCommand undo failed setting parent for group #{}: {}", group.group_id, res.message());
                restore_success = false;
                break;
            }
        }
    }

    if (!restore_success) {
        for (EntityID gid : restored_group_ids) {
            if (m_em->is_alive(gid)) m_em->destroy_entity(gid);
        }
        TransformSystem::update(*m_em);
        return false;
    }

    // Step 2: Reconnect children to groups
    struct ChildMovedRecord {
        EntityID child{0};
        EntityID target_parent{0};
        TransformComponent old_local;
    };
    std::vector<ChildMovedRecord> moved_children;
    bool children_success = true;

    for (const auto& group : m_groups) {
        for (const auto& child : group.children) {
            if (!m_em->is_alive(child.entity)) {
                children_success = false;
                break;
            }
            const auto res = m_em->set_parent(child.entity, group.group_id);
            if (!res.succeeded()) {
                ENGINE_LOG_ERROR("[HIERARCHY] UngroupEntitiesCommand undo failed reparenting child #{}: {}", child.entity, res.message());
                children_success = false;
                break;
            }
            if (m_em->has_component<TransformComponent>(child.entity)) {
                m_em->get_component<TransformComponent>(child.entity) = child.old_local_transform;
            }
            moved_children.push_back({child.entity, group.group_parent, child.new_local_transform});
        }
        if (!children_success) break;
    }

    if (!children_success) {
        // Rollback children back to group_parent
        for (const auto& rec : moved_children) {
            if (rec.target_parent == 0) {
                static_cast<void>(m_em->remove_parent(rec.child));
            } else {
                static_cast<void>(m_em->set_parent(rec.child, rec.target_parent));
            }
            if (m_em->has_component<TransformComponent>(rec.child)) {
                m_em->get_component<TransformComponent>(rec.child) = rec.old_local;
            }
        }
        for (EntityID gid : restored_group_ids) {
            if (m_em->is_alive(gid)) m_em->destroy_entity(gid);
        }
        TransformSystem::update(*m_em);
        return false;
    }

    TransformSystem::update(*m_em);

    if (m_selection) {
        std::unordered_set<EntityID> sel;
        for (EntityID id : m_original_selection) {
            if (m_em->is_alive(id)) {
                sel.insert(id);
            }
        }
        m_selection->set_selection(sel);
    }
    return true;
}

} // namespace Engine
