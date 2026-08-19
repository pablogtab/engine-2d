#include "ecs/EntityManager.hpp"
#include "ecs/components/CoreComponents.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace Engine;

namespace {

struct ValueComponent {
    int value{0};
};

struct ExcludedComponent {};

void test_entity_reuse_and_stable_id_restoration() {
    EntityManager em;
    const EntityID first = em.create_entity();
    const EntityID second = em.create_entity();
    assert(first == 1);
    assert(second == 2);

    em.destroy_entity(first);
    assert(!em.is_alive(first));
    assert(em.create_entity() == first);

    em.destroy_entity(second);
    assert(em.create_entity_with_id(second) == second);
    assert(em.is_alive(second));
    assert(em.create_entity_with_id(second) == 0);
    assert(em.create_entity_with_id(0) == 0);
}

void test_invalid_component_access_is_rejected() {
    EntityManager em;
    bool add_threw = false;
    try {
        em.add_component<ValueComponent>(42, ValueComponent{7});
    } catch (const std::invalid_argument&) {
        add_threw = true;
    }
    assert(add_threw);
    assert(!em.has_component<ValueComponent>(42));
    assert(!em.remove_component<ValueComponent>(42));

    const EntityID entity = em.create_entity();
    bool get_threw = false;
    try {
        static_cast<void>(em.get_component<ValueComponent>(entity));
    } catch (const std::runtime_error&) {
        get_threw = true;
    }
    assert(get_threw);
}

void test_component_lifecycle_clone_and_filtered_views() {
    EntityManager em;
    int constructed = 0;
    int destroyed = 0;
    em.on_construct<ValueComponent>([&](EntityID, ValueComponent&) { ++constructed; });
    em.on_destroy<ValueComponent>([&](EntityID, ValueComponent&) { ++destroyed; });

    const EntityID source = em.create_entity();
    em.add_component<ValueComponent>(source, ValueComponent{7});
    assert(constructed == 1);

    const EntityID clone = em.clone_entity(source);
    assert(clone != 0);
    assert(em.get_component<ValueComponent>(clone).value == 7);
    assert(constructed == 2);

    const EntityID excluded = em.create_entity();
    em.add_component<ValueComponent>(excluded, ValueComponent{9});
    em.add_component<ExcludedComponent>(excluded, ExcludedComponent{});

    std::vector<EntityID> filtered;
    em.for_each<ValueComponent>(exclude<ExcludedComponent>, [&](EntityID entity, ValueComponent&) {
        filtered.push_back(entity);
    });
    assert(filtered.size() == 2);
    assert(filtered[0] == source);
    assert(filtered[1] == clone);

    assert(em.remove_component<ValueComponent>(source));
    assert(!em.remove_component<ValueComponent>(source));
    assert(destroyed == 1);
    em.destroy_entity(clone);
    assert(destroyed == 2);
}

void test_hierarchy_rejects_invalid_relations_atomically() {
    EntityManager em;
    const EntityID root = em.create_entity();
    const EntityID child = em.create_entity();
    const EntityID grandchild = em.create_entity();

    assert(em.set_parent(child, root).status == HierarchyOperationStatus::Changed);
    assert(em.set_parent(grandchild, child).status == HierarchyOperationStatus::Changed);
    assert(em.set_parent(child, root).status == HierarchyOperationStatus::Unchanged);

    const auto& root_children = em.get_component<HierarchyComponent>(root).children;
    assert(root_children == std::vector<EntityID>{child});
    assert(em.get_component<HierarchyComponent>(child).parent == root);

    const HierarchyOperationResult cycle = em.set_parent(root, grandchild);
    assert(cycle.status == HierarchyOperationStatus::CycleDetected);
    assert(!cycle.succeeded());
    assert(em.get_component<HierarchyComponent>(child).parent == root);
    assert(em.get_component<HierarchyComponent>(grandchild).parent == child);

    assert(em.set_parent(root, root).status == HierarchyOperationStatus::SelfParent);
    assert(em.set_parent(child, 9999).status == HierarchyOperationStatus::InvalidParent);
    assert(em.set_parent(9999, root).status == HierarchyOperationStatus::InvalidChild);
    assert(em.get_component<HierarchyComponent>(child).parent == root);
}

void test_unparent_and_destroy_preserve_reciprocal_links() {
    EntityManager em;
    const EntityID root = em.create_entity();
    const EntityID child = em.create_entity();

    assert(em.set_parent(child, root).changed());
    assert(em.remove_parent(child).changed());
    assert(em.remove_parent(child).status == HierarchyOperationStatus::Unchanged);
    assert(em.get_component<HierarchyComponent>(child).parent == 0);
    assert(em.get_component<HierarchyComponent>(root).children.empty());
    assert(em.remove_parent(9999).status == HierarchyOperationStatus::InvalidChild);

    assert(em.set_parent(child, root).changed());
    em.destroy_entity(root);
    assert(em.is_alive(child));
    assert(em.get_component<HierarchyComponent>(child).parent == 0);
}

} // namespace

int main() {
    test_entity_reuse_and_stable_id_restoration();
    test_invalid_component_access_is_rejected();
    test_component_lifecycle_clone_and_filtered_views();
    test_hierarchy_rejects_invalid_relations_atomically();
    test_unparent_and_destroy_preserve_reciprocal_links();
    std::cout << "Entity manager tests passed\n";
    return 0;
}
