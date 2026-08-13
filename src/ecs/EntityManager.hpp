#pragma once

#include "ComponentArray.hpp"
#include "Components.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#include <typeindex>
#include <functional>

namespace Engine {

class EntityManager {
public:
    EntityManager() = default;
    ~EntityManager() = default;

    EntityManager(const EntityManager&) = delete;
    EntityManager& operator=(const EntityManager&) = delete;

    // Create a new unique Entity ID (recycles old IDs for zero allocations)
    EntityID create_entity();

    // Destroy an entity and purge its components
    void destroy_entity(EntityID entity);

    // Check if an entity ID is active
    bool is_alive(EntityID entity) const;

    // Parent-Child Hierarchy Management
    void set_parent(EntityID child, EntityID parent);
    void remove_parent(EntityID child);

    // Add / attach a component T to an entity
    template<typename T>
    T& add_component(EntityID entity, T component) {
        get_component_array<T>()->insert_data(entity, component);
        return get_component_array<T>()->get_data(entity);
    }

    // Get reference to component T on entity
    template<typename T>
    T& get_component(EntityID entity) {
        return get_component_array<T>()->get_data(entity);
    }

    // Check if entity possesses component T
    template<typename T>
    bool has_component(EntityID entity) const {
        auto* array = get_component_array_const<T>();
        return array && array->has_data(entity);
    }

    // Remove component T from entity
    template<typename T>
    void remove_component(EntityID entity) {
        get_component_array<T>()->remove_data(entity);
    }

    // Iterate efficiently over entities containing all specified component types (T1, T2...)
    template<typename PrimaryComponent, typename... OtherComponents, typename Func>
    void for_each(Func func) {
        auto* primary_array = get_component_array<PrimaryComponent>();
        if (!primary_array) return;

        for (EntityID entity : m_living_entities) {
            if (has_component<PrimaryComponent>(entity) && (has_component<OtherComponents>(entity) && ...)) {
                func(entity, get_component<PrimaryComponent>(entity), get_component<OtherComponents>(entity)...);
            }
        }
    }

    // Clear all entities and component arrays
    void clear();

    size_t get_living_entity_count() const { return m_living_entities.size(); }

private:
    template<typename T>
    ComponentArray<T>* get_component_array() {
        std::type_index type_idx(typeid(T));
        if (m_component_arrays.find(type_idx) == m_component_arrays.end()) {
            m_component_arrays[type_idx] = std::make_unique<ComponentArray<T>>();
        }
        return static_cast<ComponentArray<T>*>(m_component_arrays[type_idx].get());
    }

    template<typename T>
    const ComponentArray<T>* get_component_array_const() const {
        std::type_index type_idx(typeid(T));
        auto it = m_component_arrays.find(type_idx);
        if (it == m_component_arrays.end()) {
            return nullptr;
        }
        return static_cast<const ComponentArray<T>*>(it->second.get());
    }

    EntityID m_next_entity_id{1};
    std::vector<EntityID> m_free_entities;
    std::vector<EntityID> m_living_entities;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentArray>> m_component_arrays;
};

} // namespace Engine
