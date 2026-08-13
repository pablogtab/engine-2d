#pragma once

#include "IComponentArray.hpp"
#include <vector>
#include <unordered_map>
#include <cassert>
#include <stdexcept>

namespace Engine {

template<typename T>
class ComponentArray : public IComponentArray {
public:
    void insert_data(EntityID entity, T component) {
        if (m_entity_to_index_map.find(entity) != m_entity_to_index_map.end()) {
            // Replace existing component data
            size_t index = m_entity_to_index_map[entity];
            m_component_array[index] = component;
            return;
        }

        size_t new_index = m_component_array.size();
        m_entity_to_index_map[entity] = new_index;
        m_index_to_entity_map[new_index] = entity;
        m_component_array.push_back(component);
    }

    void remove_data(EntityID entity) {
        auto it = m_entity_to_index_map.find(entity);
        if (it == m_entity_to_index_map.end()) return;

        size_t removed_index = it->second;
        size_t last_index = m_component_array.size() - 1;

        // Move last component into removed element's place (Swap-and-Pop O(1))
        m_component_array[removed_index] = m_component_array[last_index];

        // Update map for moved element
        EntityID last_entity = m_index_to_entity_map[last_index];
        m_entity_to_index_map[last_entity] = removed_index;
        m_index_to_entity_map[removed_index] = last_entity;

        // Erase removed entity maps and pop back
        m_entity_to_index_map.erase(entity);
        m_index_to_entity_map.erase(last_index);
        m_component_array.pop_back();
    }

    T& get_data(EntityID entity) {
        auto it = m_entity_to_index_map.find(entity);
        if (it == m_entity_to_index_map.end()) {
            throw std::runtime_error("Retrieving non-existent component!");
        }
        return m_component_array[it->second];
    }

    const T& get_data(EntityID entity) const {
        auto it = m_entity_to_index_map.find(entity);
        if (it == m_entity_to_index_map.end()) {
            throw std::runtime_error("Retrieving non-existent component!");
        }
        return m_component_array[it->second];
    }

    bool has_data(EntityID entity) const {
        return m_entity_to_index_map.find(entity) != m_entity_to_index_map.end();
    }

    void entity_destroyed(EntityID entity) override {
        if (has_data(entity)) {
            remove_data(entity);
        }
    }

    void clear() {
        m_component_array.clear();
        m_entity_to_index_map.clear();
        m_index_to_entity_map.clear();
    }

    size_t size() const { return m_component_array.size(); }

private:
    std::vector<T> m_component_array;
    std::unordered_map<EntityID, size_t> m_entity_to_index_map;
    std::unordered_map<size_t, EntityID> m_index_to_entity_map;
};

} // namespace Engine
