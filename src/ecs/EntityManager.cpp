#include "EntityManager.hpp"
#include "../core/Log.hpp"
#include <algorithm>

namespace Engine {

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
            remove_parent(child);
        }
        remove_parent(entity);
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
            set_parent(dst, parent_id);
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

void EntityManager::set_parent(EntityID child, EntityID parent) {
    if (child == 0 || child == parent || !is_alive(child)) return;

    // Remove old parent reference if exists
    remove_parent(child);

    if (parent != 0 && is_alive(parent)) {
        // Ensure child has HierarchyComponent pointing to parent
        if (!has_component<HierarchyComponent>(child)) {
            add_component<HierarchyComponent>(child, HierarchyComponent{parent});
        } else {
            get_component<HierarchyComponent>(child).parent = parent;
        }

        // Ensure parent has HierarchyComponent listing child
        if (!has_component<HierarchyComponent>(parent)) {
            HierarchyComponent parent_hier{0};
            parent_hier.children.push_back(child);
            add_component<HierarchyComponent>(parent, parent_hier);
        } else {
            auto& children = get_component<HierarchyComponent>(parent).children;
            if (std::find(children.begin(), children.end(), child) == children.end()) {
                children.push_back(child);
            }
        }
    }
}

void EntityManager::remove_parent(EntityID child) {
    if (child == 0 || !has_component<HierarchyComponent>(child)) return;

    EntityID old_parent = get_component<HierarchyComponent>(child).parent;
    get_component<HierarchyComponent>(child).parent = 0;

    if (old_parent != 0 && has_component<HierarchyComponent>(old_parent)) {
        auto& children = get_component<HierarchyComponent>(old_parent).children;
        children.erase(std::remove(children.begin(), children.end(), child), children.end());
    }
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
