#include "EntityManager.hpp"
#include "components/CoreComponents.hpp"
#include <algorithm>
#include <limits>
#include <unordered_set>

namespace Engine {

const char* HierarchyOperationResult::message() const {
    switch (status) {
        case HierarchyOperationStatus::Changed: return "Hierarchy changed";
        case HierarchyOperationStatus::Unchanged: return "Hierarchy already matches the request";
        case HierarchyOperationStatus::InvalidChild: return "Child entity does not exist";
        case HierarchyOperationStatus::InvalidParent: return "Parent entity does not exist";
        case HierarchyOperationStatus::SelfParent: return "An entity cannot be parented to itself";
        case HierarchyOperationStatus::CycleDetected: return "Reparenting would create a hierarchy cycle";
        case HierarchyOperationStatus::MissingTransform: return "Hierarchy requires TransformComponent on both child and parent entities";
    }
    return "Unknown hierarchy result";
}

void EntityManager::swap(EntityManager& other) noexcept {
    using std::swap;
    swap(m_next_entity_id, other.m_next_entity_id);
    swap(m_alive_entities, other.m_alive_entities);
    swap(m_free_entities, other.m_free_entities);
    swap(m_entity_generations, other.m_entity_generations);
    swap(m_living_entities, other.m_living_entities);
    swap(m_root_entities, other.m_root_entities);
    swap(m_entity_masks, other.m_entity_masks);
    swap(m_component_arrays, other.m_component_arrays);
}

EntityID EntityManager::create_entity() {
    EntityID id;
    if (!m_free_entities.empty()) {
        id = m_free_entities.back();
        m_free_entities.pop_back();
    } else {
        if (m_next_entity_id > std::numeric_limits<EntityID>::max()) {
            throw std::overflow_error("Entity ID space exhausted");
        }
        id = static_cast<EntityID>(m_next_entity_id++);
    }

    auto& generation = m_entity_generations[id];
    if (++generation == 0) ++generation;
    m_alive_entities.insert(id);
    m_living_entities.push_back(id);
    m_root_entities.push_back(id);
    return id;
}

EntityID EntityManager::create_entity_with_id(EntityID entity) {
    if (entity == 0 || is_alive(entity)) return 0;

    auto free_it = std::find(m_free_entities.begin(), m_free_entities.end(), entity);
    if (free_it != m_free_entities.end()) {
        m_free_entities.erase(free_it);
    }

    auto& generation = m_entity_generations[entity];
    if (++generation == 0) ++generation;
    m_alive_entities.insert(entity);
    m_living_entities.push_back(entity);
    m_root_entities.push_back(entity);
    m_next_entity_id = std::max(m_next_entity_id, static_cast<std::uint64_t>(entity) + 1);
    return entity;
}

void EntityManager::destroy_entity(EntityID entity) {
    auto it = std::find(m_living_entities.begin(), m_living_entities.end(), entity);
    if (it == m_living_entities.end()) return;

    // Clean up hierarchy links before destroying entity
    if (has_component<HierarchyComponent>(entity)) {
        auto children_copy = get_component<HierarchyComponent>(entity).children;
        for (EntityID child : children_copy) {
            static_cast<void>(remove_parent(child));
        }
        static_cast<void>(remove_parent(entity));
    }

    // Purge entity from all component arrays
    for (auto& pair : m_component_arrays) {
        if (pair.second) {
            pair.second->entity_destroyed(entity);
        }
    }

    m_alive_entities.erase(entity);
    m_entity_masks.erase(entity);
    m_living_entities.erase(it);

    auto root_it = std::find(m_root_entities.begin(), m_root_entities.end(), entity);
    if (root_it != m_root_entities.end()) {
        m_root_entities.erase(root_it);
    }

    m_free_entities.push_back(entity);
}

void EntityManager::destroy_entity_subtree(EntityID entity) {
    if (!is_alive(entity)) return;

    std::vector<EntityID> descendants;
    std::vector<EntityID> pending{entity};
    std::unordered_set<EntityID> visited;
    while (!pending.empty()) {
        const EntityID current = pending.back();
        pending.pop_back();
        if (!is_alive(current) || !visited.insert(current).second) continue;
        descendants.push_back(current);
        if (!has_component<HierarchyComponent>(current)) continue;
        const auto& children = get_component<HierarchyComponent>(current).children;
        pending.insert(pending.end(), children.begin(), children.end());
    }

    for (auto it = descendants.rbegin(); it != descendants.rend(); ++it) {
        destroy_entity(*it);
    }
}

EntityID EntityManager::clone_entity(EntityID src) {
    if (!is_alive(src)) return 0;

    EntityID dst = create_entity();
    for (auto& pair : m_component_arrays) {
        if (pair.second) {
            pair.second->clone_data(src, dst);
        }
    }
    m_entity_masks[dst] = m_entity_masks[src];

    // Ensure cloned entity starts with a fresh children list
    if (has_component<HierarchyComponent>(dst)) {
        get_component<HierarchyComponent>(dst).children.clear();
    }

    // If cloned entity has a parent, automatically register it into the parent's children list!
    if (has_component<HierarchyComponent>(src)) {
        EntityID parent_id = get_component<HierarchyComponent>(src).parent;
        if (parent_id != 0 && is_alive(parent_id)) {
            get_component<HierarchyComponent>(dst).parent = 0;
            const HierarchyOperationResult result = set_parent(dst, parent_id);
            if (!result.succeeded()) {
                destroy_entity(dst);
                return 0;
            }
        }
    }

    return dst;
}

EntityID EntityManager::find_by_tag(const std::string& tag) {
    EntityID result = 0;
    for_each<TagComponent>([&result, &tag](EntityID id, const TagComponent& comp) {
        if (result == 0 && comp.tag == tag) {
            result = id;
        }
    });
    return result;
}

std::vector<EntityID> EntityManager::find_all_by_tag(const std::string& tag) {
    std::vector<EntityID> result;
    for_each<TagComponent>([&result, &tag](EntityID id, const TagComponent& comp) {
        if (comp.tag == tag) {
            result.push_back(id);
        }
    });
    return result;
}

bool EntityManager::is_alive(EntityID entity) const {
    return m_alive_entities.contains(entity);
}

std::uint64_t EntityManager::entity_generation(EntityID entity) const {
    const auto found = m_entity_generations.find(entity);
    return found == m_entity_generations.end() ? 0 : found->second;
}

bool EntityManager::would_create_hierarchy_cycle(EntityID child, EntityID parent) const {
    std::unordered_set<EntityID> visited;
    EntityID current = parent;
    while (current != 0) {
        if (current == child) return true;
        if (!is_alive(current)) return true;
        if (!visited.insert(current).second) return true;
        if (!has_component<HierarchyComponent>(current)) return false;
        current = get_component<HierarchyComponent>(current).parent;
    }
    return false;
}

HierarchyOperationResult EntityManager::set_parent(EntityID child, EntityID parent, std::optional<size_t> target_index) {
    if (child == 0 || !is_alive(child)) return {HierarchyOperationStatus::InvalidChild};
    if (!has_component<TransformComponent>(child)) return {HierarchyOperationStatus::MissingTransform};
    if (parent == 0) return remove_parent(child, target_index);
    if (!is_alive(parent)) return {HierarchyOperationStatus::InvalidParent};
    if (!has_component<TransformComponent>(parent)) return {HierarchyOperationStatus::MissingTransform};
    if (child == parent) return {HierarchyOperationStatus::SelfParent};
    if (would_create_hierarchy_cycle(child, parent)) {
        return {HierarchyOperationStatus::CycleDetected};
    }

    EntityID old_parent = 0;
    if (has_component<HierarchyComponent>(child)) {
        old_parent = get_component<HierarchyComponent>(child).parent;
    }

    if (old_parent == parent) {
        if (!has_component<HierarchyComponent>(parent)) {
            HierarchyComponent parent_hierarchy;
            parent_hierarchy.children.push_back(child);
            add_component<HierarchyComponent>(parent, std::move(parent_hierarchy));
            return {HierarchyOperationStatus::Changed};
        }
        auto& children = get_component<HierarchyComponent>(parent).children;
        const std::size_t occurrences = static_cast<std::size_t>(
            std::count(children.begin(), children.end(), child)
        );
        if (occurrences == 1) {
            if (!target_index.has_value()) {
                return {HierarchyOperationStatus::Unchanged};
            }
            auto it = std::find(children.begin(), children.end(), child);
            if (it != children.end()) {
                size_t cur_idx = static_cast<size_t>(std::distance(children.begin(), it));
                size_t clamped_target = *target_index >= children.size() ? children.size() - 1 : *target_index;
                if (cur_idx == clamped_target) {
                    return {HierarchyOperationStatus::Unchanged};
                }
            }
        }
        children.erase(std::remove(children.begin(), children.end(), child), children.end());
        if (target_index.has_value() && *target_index < children.size()) {
            children.insert(children.begin() + *target_index, child);
        } else {
            children.push_back(child);
        }
        return {HierarchyOperationStatus::Changed};
    }

    static_cast<void>(remove_parent(child));

    // Remove from m_root_entities now that it has a non-zero parent
    auto root_it = std::find(m_root_entities.begin(), m_root_entities.end(), child);
    if (root_it != m_root_entities.end()) {
        m_root_entities.erase(root_it);
    }

    if (!has_component<HierarchyComponent>(child)) {
        add_component<HierarchyComponent>(child, HierarchyComponent{parent});
    } else {
        get_component<HierarchyComponent>(child).parent = parent;
    }

    if (!has_component<HierarchyComponent>(parent)) {
        HierarchyComponent parent_hierarchy;
        parent_hierarchy.children.push_back(child);
        add_component<HierarchyComponent>(parent, std::move(parent_hierarchy));
    } else {
        auto& children = get_component<HierarchyComponent>(parent).children;
        children.erase(std::remove(children.begin(), children.end(), child), children.end());
        if (target_index.has_value() && *target_index < children.size()) {
            children.insert(children.begin() + *target_index, child);
        } else {
            children.push_back(child);
        }
    }
    return {HierarchyOperationStatus::Changed};
}

HierarchyOperationResult EntityManager::remove_parent(EntityID child, std::optional<size_t> target_index) {
    if (child == 0 || !is_alive(child)) return {HierarchyOperationStatus::InvalidChild};
    if (!has_component<HierarchyComponent>(child)) {
        // Already root entity
        if (target_index.has_value()) {
            static_cast<void>(reorder_sibling(child, *target_index));
        }
        return {HierarchyOperationStatus::Unchanged};
    }

    EntityID old_parent = get_component<HierarchyComponent>(child).parent;
    if (old_parent == 0) {
        // Already root entity
        if (target_index.has_value()) {
            static_cast<void>(reorder_sibling(child, *target_index));
        }
        return {HierarchyOperationStatus::Unchanged};
    }
    get_component<HierarchyComponent>(child).parent = 0;

    if (is_alive(old_parent) && has_component<HierarchyComponent>(old_parent)) {
        auto& children = get_component<HierarchyComponent>(old_parent).children;
        children.erase(std::remove(children.begin(), children.end(), child), children.end());
    }

    // Add to m_root_entities
    auto root_it = std::find(m_root_entities.begin(), m_root_entities.end(), child);
    if (root_it == m_root_entities.end()) {
        if (target_index.has_value() && *target_index < m_root_entities.size()) {
            m_root_entities.insert(m_root_entities.begin() + *target_index, child);
        } else {
            m_root_entities.push_back(child);
        }
    } else if (target_index.has_value()) {
        static_cast<void>(reorder_sibling(child, *target_index));
    }

    return {HierarchyOperationStatus::Changed};
}

size_t EntityManager::get_sibling_index(EntityID entity) const {
    if (!is_alive(entity)) return 0;

    if (has_component<HierarchyComponent>(entity)) {
        EntityID parent_id = get_component<HierarchyComponent>(entity).parent;
        if (parent_id != 0 && is_alive(parent_id) && has_component<HierarchyComponent>(parent_id)) {
            const auto& children = get_component<HierarchyComponent>(parent_id).children;
            auto it = std::find(children.begin(), children.end(), entity);
            if (it != children.end()) {
                return static_cast<size_t>(std::distance(children.begin(), it));
            }
        }
    }

    // Root entity: lookup in m_root_entities
    auto it = std::find(m_root_entities.begin(), m_root_entities.end(), entity);
    if (it != m_root_entities.end()) {
        return static_cast<size_t>(std::distance(m_root_entities.begin(), it));
    }
    return 0;
}

size_t EntityManager::get_sibling_count(EntityID entity) const {
    if (!is_alive(entity)) return 0;

    if (has_component<HierarchyComponent>(entity)) {
        EntityID parent_id = get_component<HierarchyComponent>(entity).parent;
        if (parent_id != 0 && is_alive(parent_id) && has_component<HierarchyComponent>(parent_id)) {
            return get_component<HierarchyComponent>(parent_id).children.size();
        }
    }

    // Root entities count
    return m_root_entities.size();
}

bool EntityManager::reorder_sibling(EntityID entity, size_t target_index) {
    if (!is_alive(entity)) return false;

    if (has_component<HierarchyComponent>(entity)) {
        EntityID parent_id = get_component<HierarchyComponent>(entity).parent;
        if (parent_id != 0 && is_alive(parent_id) && has_component<HierarchyComponent>(parent_id)) {
            auto& children = get_component<HierarchyComponent>(parent_id).children;
            auto it = std::find(children.begin(), children.end(), entity);
            if (it == children.end()) return false;
            size_t current_idx = static_cast<size_t>(std::distance(children.begin(), it));
            if (target_index >= children.size()) target_index = children.size() - 1;
            if (current_idx == target_index) return true;

            children.erase(it);
            children.insert(children.begin() + target_index, entity);
            return true;
        }
    }

    // Root entity reordering in m_root_entities ONLY (m_living_entities is untouched!)
    auto it = std::find(m_root_entities.begin(), m_root_entities.end(), entity);
    if (it == m_root_entities.end()) return false;

    size_t current_root_idx = static_cast<size_t>(std::distance(m_root_entities.begin(), it));
    if (target_index >= m_root_entities.size()) target_index = m_root_entities.size() - 1;
    if (current_root_idx == target_index) return true;

    m_root_entities.erase(it);
    m_root_entities.insert(m_root_entities.begin() + target_index, entity);
    return true;
}

void EntityManager::clear() {
    for (auto& pair : m_component_arrays) {
        if (pair.second) {
            // Memory arrays cleared
        }
    }
    m_component_arrays.clear();
    m_alive_entities.clear();
    m_living_entities.clear();
    m_root_entities.clear();
    m_free_entities.clear();
    m_entity_masks.clear();
    m_next_entity_id = 1;
}

} // namespace Engine
