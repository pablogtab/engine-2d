#include "HierarchyCommands.hpp"
#include "../../ecs/TransformSystem.hpp"
#include "../../core/Log.hpp"
#include <algorithm>
#include <unordered_set>

namespace Engine {

// ---------------------------------------------------------------------------
// 1. ReparentEntityCommand
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// 2. GroupSelectedEntitiesCommand
// ---------------------------------------------------------------------------
GroupSelectedEntitiesCommand::GroupSelectedEntitiesCommand(
    EntityManager* em,
    std::shared_ptr<EditorSelection> selection,
    const std::vector<EntityID>& selected_entities,
    std::string group_name
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
    const std::vector<EntityID>& target_entities
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

    // 2. Preflight each group and its children
    m_groups.clear();
    m_unpacked_children.clear();

    for (EntityID group_id : root_groups) {
        EntityID group_parent = m_em->has_component<HierarchyComponent>(group_id)
            ? m_em->get_component<HierarchyComponent>(group_id).parent : 0;

        const TransformComponent* parent_world = (group_parent != 0 && m_em->has_component<TransformComponent>(group_parent))
            ? &m_em->get_component<TransformComponent>(group_parent) : nullptr;

        std::vector<EntityID> direct_children;
        if (m_em->has_component<HierarchyComponent>(group_id)) {
            direct_children = m_em->get_component<HierarchyComponent>(group_id).children;
        }

        std::vector<UngroupChildEntry> child_entries;
        for (EntityID child : direct_children) {
            if (!m_em->is_alive(child) || !m_em->has_component<TransformComponent>(child)) continue;

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
