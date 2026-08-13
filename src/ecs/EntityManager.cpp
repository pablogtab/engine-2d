#include "EntityManager.hpp"
#include "../core/Log.hpp"
#include <algorithm>

namespace Engine {

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

void EntityManager::destroy_entity(EntityID entity) {
    auto it = std::find(m_living_entities.begin(), m_living_entities.end(), entity);
    if (it == m_living_entities.end()) return;

    // Purge entity from all component arrays
    for (auto& pair : m_component_arrays) {
        if (pair.second) {
            pair.second->entity_destroyed(entity);
        }
    }

    m_living_entities.erase(it);
    m_free_entities.push_back(entity);
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
    m_next_entity_id = 1;
}

} // namespace Engine
