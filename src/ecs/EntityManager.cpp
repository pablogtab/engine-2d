#include "EntityManager.hpp"
#include "components/CoreComponents.hpp"
#include <algorithm>
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
    }
    return "Unknown hierarchy result";
}

void EntityManager::swap(EntityManager& other) noexcept {
    using std::swap;
    swap(m_next_entity_id, other.m_next_entity_id);
    swap(m_free_entities, other.m_free_entities);
    swap(m_living_entities, other.m_living_entities);
    swap(m_entity_masks, other.m_entity_masks);
    swap(m_component_arrays, other.m_component_arrays);
}

EntityID EntityManager::create_entity() {
    EntityID id;
    if (!m_free_entities.empty()) {
        id = m_free_entities.back();
        m_free_entities.pop_back();
    } else {
        id = m_next_entity_id++;
    }

    m_living_entities.push_back(id);
    return id;
}

EntityID EntityManager::create_entity_with_id(EntityID entity) {
    if (entity == 0 || is_alive(entity)) return 0;

    auto free_it = std::find(m_free_entities.begin(), m_free_entities.end(), entity);
    if (free_it != m_free_entities.end()) {
        m_free_entities.erase(free_it);
    }

    m_living_entities.push_back(entity);
    m_next_entity_id = std::max(m_next_entity_id, entity + 1);
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

    m_entity_masks.erase(entity);
    m_living_entities.erase(it);
    m_free_entities.push_back(entity);
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
    return std::find(m_living_entities.begin(), m_living_entities.end(), entity) != m_living_entities.end();
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

HierarchyOperationResult EntityManager::set_parent(EntityID child, EntityID parent) {
    if (child == 0 || !is_alive(child)) return {HierarchyOperationStatus::InvalidChild};
    if (parent == 0) return remove_parent(child);
    if (!is_alive(parent)) return {HierarchyOperationStatus::InvalidParent};
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
        if (occurrences == 1) return {HierarchyOperationStatus::Unchanged};
        children.erase(std::remove(children.begin(), children.end(), child), children.end());
        children.push_back(child);
        return {HierarchyOperationStatus::Changed};
    }

    static_cast<void>(remove_parent(child));

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
        children.push_back(child);
    }
    return {HierarchyOperationStatus::Changed};
}

HierarchyOperationResult EntityManager::remove_parent(EntityID child) {
    if (child == 0 || !is_alive(child)) return {HierarchyOperationStatus::InvalidChild};
    if (!has_component<HierarchyComponent>(child)) {
        return {HierarchyOperationStatus::Unchanged};
    }

    EntityID old_parent = get_component<HierarchyComponent>(child).parent;
    if (old_parent == 0) return {HierarchyOperationStatus::Unchanged};
    get_component<HierarchyComponent>(child).parent = 0;

    if (is_alive(old_parent) && has_component<HierarchyComponent>(old_parent)) {
        auto& children = get_component<HierarchyComponent>(old_parent).children;
        children.erase(std::remove(children.begin(), children.end(), child), children.end());
    }
    return {HierarchyOperationStatus::Changed};
}

void EntityManager::clear() {
    for (auto& pair : m_component_arrays) {
        if (pair.second) {
            // Memory arrays cleared
        }
    }
    m_component_arrays.clear();
    m_living_entities.clear();
    m_free_entities.clear();
    m_entity_masks.clear();
    m_next_entity_id = 1;
}

} // namespace Engine
