#include "editor/state/EditorEntityStateStore.hpp"
#include "editor/commands/EditorEntityStateCommands.hpp"
#include "editor/commands/EditorCommandHistory.hpp"
#include "ecs/EntityManager.hpp"
#include <cassert>
#include <iostream>

using namespace Engine;

void test_default_state() {
    EditorEntityStateStore store;
    assert(!store.is_locked(1));
    assert(store.is_visible(1));

    EditorEntityState s = store.get(1);
    assert(!s.locked);
    assert(s.visible_in_editor);
    std::cout << "[PASS] test_default_state" << std::endl;
}

void test_mutations() {
    EditorEntityStateStore store;
    store.set_locked(10, true);
    assert(store.is_locked(10));
    assert(store.is_visible(10));

    store.set_visible(10, false);
    assert(store.is_locked(10));
    assert(!store.is_visible(10));

    store.set_locked(10, false);
    assert(!store.is_locked(10));
    assert(!store.is_visible(10));

    store.remove(10);
    assert(!store.is_locked(10));
    assert(store.is_visible(10));
    std::cout << "[PASS] test_mutations" << std::endl;
}

void test_state_snapshot_is_sorted_and_complete() {
    EditorEntityStateStore store;
    const std::uint64_t initial_revision = store.persistence_revision();
    store.set_visible(9, false);
    store.set_locked(3, true);
    store.set_component_order(6, {"transform", "sprite"});

    assert(store.persistence_revision() == initial_revision + 2);
    store.set_locked(3, true);
    assert(store.persistence_revision() == initial_revision + 2);

    const auto states = store.states();
    assert(states.size() == 3);
    assert(states[0].first == 3 && states[0].second.locked);
    assert(states[1].first == 6 && states[1].second.component_order.size() == 2);
    assert(states[2].first == 9 && !states[2].second.visible_in_editor);
    std::cout << "[PASS] test_state_snapshot_is_sorted_and_complete" << std::endl;
}

void test_component_order_resolution_and_authoring_command() {
    EntityManager em;
    const EntityID entity = em.create_entity();
    auto store = std::make_shared<EditorEntityStateStore>();

    store->set_component_order(entity, {"sprite", "transform", "missing"});
    assert((store->get_component_order(entity) ==
        std::vector<std::string>{"sprite", "transform", "missing"}));
    assert((store->resolve_component_order(entity, {"transform", "camera", "sprite"}) ==
        std::vector<std::string>{"sprite", "transform", "camera"}));

    EditorCommandHistory history;
    history.execute_command(std::make_unique<SetComponentOrderCommand>(
        &em,
        store,
        entity,
        std::vector<std::string>{"sprite", "transform", "camera"},
        std::vector<std::string>{"camera", "sprite", "transform"}
    ));
    assert((store->get_component_order(entity) ==
        std::vector<std::string>{"camera", "sprite", "transform"}));
    assert(history.is_dirty());

    history.undo();
    assert((store->get_component_order(entity) ==
        std::vector<std::string>{"sprite", "transform", "camera"}));
    history.redo();
    assert((store->get_component_order(entity) ==
        std::vector<std::string>{"camera", "sprite", "transform"}));

    store->set_locked(entity, true);
    store->set_component_order(entity, {});
    assert(store->is_locked(entity));
    assert(store->get_component_order(entity).empty());
    std::cout << "[PASS] test_component_order_resolution_and_authoring_command" << std::endl;
}

void test_component_open_state_persistence() {
    EditorEntityStateStore store;
    const std::uint64_t initial_revision = store.persistence_revision();

    assert(!store.component_open_state(12, "sprite").has_value());
    store.set_component_open(12, "sprite", false);
    assert(store.component_open_state(12, "sprite") == false);
    assert(store.persistence_revision() == initial_revision + 1);

    store.set_component_open(12, "sprite", false);
    assert(store.persistence_revision() == initial_revision + 1);
    store.set_component_open(12, "sprite", true);
    assert(store.component_open_state(12, "sprite") == true);
    assert(store.persistence_revision() == initial_revision + 2);

    EditorEntityState restored = store.get(12);
    EditorEntityStateStore copy;
    copy.set(12, restored);
    assert(copy.component_open_state(12, "sprite") == true);
    assert(copy.states().size() == 1);
    std::cout << "[PASS] test_component_open_state_persistence" << std::endl;
}

void test_pruning() {
    EntityManager em;
    EntityID e1 = em.create_entity();
    EntityID e2 = em.create_entity();
    EntityID e3 = em.create_entity();

    EditorEntityStateStore store;
    store.set_locked(e1, true);
    store.set_visible(e2, false);
    store.set_locked(e3, true);

    // Destroy e2 and e3
    em.destroy_entity(e2);
    em.destroy_entity(e3);

    store.prune(em);

    assert(store.is_locked(e1));
    assert(store.is_visible(e2)); // Reset to default because e2 was pruned
    assert(!store.is_locked(e3)); // Reset to default because e3 was pruned
    std::cout << "[PASS] test_pruning" << std::endl;
}

void test_state_commands_single() {
    EntityManager em;
    EntityID e1 = em.create_entity();
    auto store = std::make_shared<EditorEntityStateStore>();
    EditorCommandHistory history;

    // Lock command
    auto lock_cmd = std::make_unique<SetEditorEntityStateCommand>(&em, store, EditorEntityStateField::Locked, e1, true);
    assert(lock_cmd->get_scope() == EditorCommandScope::EditorSession);
    history.execute_command(std::move(lock_cmd));
    assert(store->is_locked(e1));
    assert(!history.is_dirty()); // Session commands should not dirty scene

    // Undo
    assert(history.can_undo());
    history.undo();
    assert(!store->is_locked(e1));
    assert(!history.is_dirty());

    // Redo
    assert(history.can_redo());
    history.redo();
    assert(store->is_locked(e1));
    assert(!history.is_dirty());

    // Visibility command
    auto vis_cmd = std::make_unique<SetEditorEntityStateCommand>(&em, store, EditorEntityStateField::VisibleInEditor, e1, false);
    history.execute_command(std::move(vis_cmd));
    assert(!store->is_visible(e1));
    assert(store->is_locked(e1));

    assert(history.can_undo());
    history.undo();
    assert(store->is_visible(e1));
    std::cout << "[PASS] test_state_commands_single" << std::endl;
}

void test_state_commands_multi() {
    EntityManager em;
    EntityID e1 = em.create_entity();
    EntityID e2 = em.create_entity();
    EntityID e3 = em.create_entity();
    auto store = std::make_shared<EditorEntityStateStore>();
    EditorCommandHistory history;

    store->set_locked(e1, true);
    store->set_locked(e2, false);
    store->set_locked(e3, false);

    std::vector<EntityID> entities = {e1, e2, e3};
    auto lock_all = std::make_unique<SetEditorEntityStateCommand>(&em, store, EditorEntityStateField::Locked, entities, true);
    history.execute_command(std::move(lock_all));
    assert(store->is_locked(e1));
    assert(store->is_locked(e2));
    assert(store->is_locked(e3));

    assert(history.can_undo());
    history.undo();
    assert(store->is_locked(e1)); // Was already locked before
    assert(!store->is_locked(e2));
    assert(!store->is_locked(e3));

    assert(history.can_redo());
    history.redo();
    assert(store->is_locked(e1));
    assert(store->is_locked(e2));
    assert(store->is_locked(e3));
    std::cout << "[PASS] test_state_commands_multi" << std::endl;
}

int main() {
    test_default_state();
    test_mutations();
    test_state_snapshot_is_sorted_and_complete();
    test_component_order_resolution_and_authoring_command();
    test_component_open_state_persistence();
    test_pruning();
    test_state_commands_single();
    test_state_commands_multi();
    std::cout << "All EditorEntityStateStore tests passed successfully!" << std::endl;
    return 0;
}
