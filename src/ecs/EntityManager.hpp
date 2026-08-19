#pragma once

#include "ComponentArray.hpp"
#include "EcsTypes.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#include <typeindex>
#include <functional>
#include <tuple>
#include <string>
#include <stdexcept>

namespace Engine {

class EntityManager {
public:
    EntityManager() = default;
    ~EntityManager() = default;

    EntityManager(const EntityManager&) = delete;
    EntityManager& operator=(const EntityManager&) = delete;
    EntityManager(EntityManager&&) noexcept = default;
    EntityManager& operator=(EntityManager&&) noexcept = default;

    void swap(EntityManager& other) noexcept;

    // Create a new unique Entity ID (recycles old IDs for zero allocations)
    EntityID create_entity();

    // Restore an entity with its original ID. Used by editor undo/redo so
    // references and commands remain stable across a delete/restore cycle.
    EntityID create_entity_with_id(EntityID entity);

    // Destroy an entity and purge its components
    void destroy_entity(EntityID entity);

    // Check if an entity ID is active
    bool is_alive(EntityID entity) const;

    // Parent-Child Hierarchy Management
    void set_parent(EntityID child, EntityID parent);
    void remove_parent(EntityID child);

    // Clone an existing entity and all its attached components
    EntityID clone_entity(EntityID src);

    // Add / attach a component T to an entity
    template<typename T>
    T& add_component(EntityID entity, T component) {
        get_component_array<T>()->insert_data(entity, component);
        m_entity_masks[entity].set(get_component_type_id<T>());
        return get_component_array<T>()->get_data(entity);
    }

    // Get reference to component T on entity
    template<typename T>
    T& get_component(EntityID entity) {
        return get_component_array<T>()->get_data(entity);
    }

    template<typename T>
    const T& get_component(EntityID entity) const {
        const ComponentArray<T>* components = get_component_array_const<T>();
        if (!components) throw std::runtime_error("Retrieving non-existent component storage");
        return components->get_data(entity);
    }

    // C++20 Structured Bindings unpacking for multiple components: auto [transform, sprite] = get_components<...>(e);
    template<typename... Components>
    std::tuple<Components&...> get_components(EntityID entity) {
        return std::tie(get_component<Components>(entity)...);
    }

    // Check if entity possesses component T
    template<typename T>
    bool has_component(EntityID entity) const {
        auto it = m_entity_masks.find(entity);
        if (it == m_entity_masks.end()) return false;
        return it->second.test(get_component_type_id<T>());
    }

    // Remove component T from entity
    template<typename T>
    void remove_component(EntityID entity) {
        get_component_array<T>()->remove_data(entity);
        auto it = m_entity_masks.find(entity);
        if (it != m_entity_masks.end()) {
            it->second.reset(get_component_type_id<T>());
        }
    }

    // Register lifecycle callback when component T is constructed/attached
    template<typename T>
    void on_construct(typename ComponentArray<T>::ComponentCallback callback) {
        get_component_array<T>()->set_on_construct(callback);
    }

    // Register lifecycle callback when component T is destroyed/removed
    template<typename T>
    void on_destroy(typename ComponentArray<T>::ComponentCallback callback) {
        get_component_array<T>()->set_on_destroy(callback);
    }

    // High-performance CPU Bitwise AND view iteration over entities with all target components
    template<typename T1, typename... OtherComponents, typename Func>
    void for_each(Func func) {
        ComponentMask required_mask;
        required_mask.set(get_component_type_id<T1>());
        (required_mask.set(get_component_type_id<OtherComponents>()), ...);

        for (EntityID entity : m_living_entities) {
            const auto& mask = m_entity_masks[entity];
            if ((mask & required_mask) == required_mask) {
                func(entity, get_component<T1>(entity), get_component<OtherComponents>(entity)...);
            }
        }
    }

    // View iteration with Exclusion Filters: for_each<T1, T2>(exclude<Ex1, Ex2>, func)
    template<typename T1, typename... OtherComponents, typename... Excludes, typename Func>
    void for_each(exclude_t<Excludes...>, Func func) {
        ComponentMask required_mask;
        required_mask.set(get_component_type_id<T1>());
        (required_mask.set(get_component_type_id<OtherComponents>()), ...);

        ComponentMask exclusion_mask;
        (exclusion_mask.set(get_component_type_id<Excludes>()), ...);

        for (EntityID entity : m_living_entities) {
            const auto& mask = m_entity_masks[entity];
            if ((mask & required_mask) == required_mask && (mask & exclusion_mask).none()) {
                func(entity, get_component<T1>(entity), get_component<OtherComponents>(entity)...);
            }
        }
    }

    // Bulk deletion of entities matching a component predicate
    template<typename T, typename Predicate>
    void destroy_matching(Predicate pred) {
        std::vector<EntityID> to_destroy;
        for_each<T>([&to_destroy, &pred](EntityID id, const T& comp) {
            if (pred(comp)) {
                to_destroy.push_back(id);
            }
        });
        for (EntityID id : to_destroy) {
            destroy_entity(id);
        }
    }

    // Fast Tag lookups O(1)
    EntityID find_by_tag(const std::string& tag);
    std::vector<EntityID> find_all_by_tag(const std::string& tag);

    // Clear all entities and component arrays
    void clear();

    size_t get_living_entity_count() const { return m_living_entities.size(); }
    [[nodiscard]] const std::vector<EntityID>& get_living_entities() const { return m_living_entities; }

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
    std::unordered_map<EntityID, ComponentMask> m_entity_masks;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentArray>> m_component_arrays;
};

} // namespace Engine
