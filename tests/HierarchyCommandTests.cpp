#include "core/Log.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/TransformSystem.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "editor/commands/EditorCommandHistory.hpp"
#include "editor/commands/HierarchyCommands.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace Engine;

static bool approx_equal(float a, float b, float eps = 0.01f) {
    return std::abs(a - b) <= eps;
}

static bool approx_equal_vec2(const glm::vec2& a, const glm::vec2& b, float eps = 0.01f) {
    return approx_equal(a.x, b.x, eps) && approx_equal(a.y, b.y, eps);
}

static bool approx_equal_vec3(const glm::vec3& a, const glm::vec3& b, float eps = 0.01f) {
    return approx_equal(a.x, b.x, eps) && approx_equal(a.y, b.y, eps) && approx_equal(a.z, b.z, eps);
}

void test_reparent_preserves_world_transform() {
    EntityManager em;
    EditorCommandHistory history(100);

    // Parent entity at (100, 50, 0), rotated 90 degrees, scaled 2.0x
    EntityID parent = em.create_entity();
    em.add_component<TagComponent>(parent, TagComponent{"Parent"});
    em.add_component<TransformComponent>(parent, TransformComponent{
        glm::vec3(100.0f, 50.0f, 0.0f),
        glm::vec2(2.0f, 2.0f),
        90.0f
    });

    // Child entity starting at world position (140, 90, 0), scale (1, 1), rotation 45
    EntityID child = em.create_entity();
    em.add_component<TagComponent>(child, TagComponent{"Child"});
    em.add_component<TransformComponent>(child, TransformComponent{
        glm::vec3(140.0f, 90.0f, 0.0f),
        glm::vec2(1.0f, 1.0f),
        45.0f
    });

    TransformSystem::update(em);

    const auto& initial_child_t = em.get_component<TransformComponent>(child);
    const glm::vec3 expected_world_pos = initial_child_t.world_position;
    const glm::vec2 expected_world_scale = initial_child_t.world_scale;
    const float expected_world_rot = initial_child_t.world_rotation;

    // Execute ReparentEntityCommand
    auto cmd = std::make_unique<ReparentEntityCommand>(&em, child, parent);
    assert(cmd->is_valid());
    history.execute_command(std::move(cmd));

    // Verify parent is set in HierarchyComponent
    assert(em.has_component<HierarchyComponent>(child));
    assert(em.get_component<HierarchyComponent>(child).parent == parent);
    assert(history.get_undo_count() == 1);

    // Verify world transform is preserved
    TransformSystem::update(em);
    const auto& reparented_child_t = em.get_component<TransformComponent>(child);
    assert(approx_equal_vec3(reparented_child_t.world_position, expected_world_pos));
    assert(approx_equal_vec2(reparented_child_t.world_scale, expected_world_scale));
    assert(approx_equal(reparented_child_t.world_rotation, expected_world_rot));

    // Test Undo
    assert(history.can_undo());
    history.undo();

    assert(em.get_component<HierarchyComponent>(child).parent == 0);
    TransformSystem::update(em);
    const auto& undone_child_t = em.get_component<TransformComponent>(child);
    assert(approx_equal_vec3(undone_child_t.position, glm::vec3(140.0f, 90.0f, 0.0f)));
    assert(approx_equal_vec2(undone_child_t.scale, glm::vec2(1.0f, 1.0f)));
    assert(approx_equal(undone_child_t.rotation, 45.0f));

    // Test Redo
    assert(history.can_redo());
    history.redo();

    assert(em.get_component<HierarchyComponent>(child).parent == parent);
    TransformSystem::update(em);
    const auto& redone_child_t = em.get_component<TransformComponent>(child);
    assert(approx_equal_vec3(redone_child_t.world_position, expected_world_pos));
    assert(approx_equal_vec2(redone_child_t.world_scale, expected_world_scale));
    assert(approx_equal(redone_child_t.world_rotation, expected_world_rot));

    std::cout << "test_reparent_preserves_world_transform passed." << std::endl;
}

void test_unparent_preserves_world_transform() {
    EntityManager em;
    EditorCommandHistory history(100);

    EntityID parent = em.create_entity();
    em.add_component<TagComponent>(parent, TagComponent{"Parent"});
    em.add_component<TransformComponent>(parent, TransformComponent{
        glm::vec3(200.0f, 100.0f, 0.0f),
        glm::vec2(2.0f, 2.0f),
        0.0f
    });

    EntityID child = em.create_entity();
    em.add_component<TagComponent>(child, TagComponent{"Child"});
    em.add_component<TransformComponent>(child, TransformComponent{
        glm::vec3(10.0f, 20.0f, 0.0f), // local to parent
        glm::vec2(1.0f, 1.0f),
        0.0f
    });

    const auto set_parent_res = em.set_parent(child, parent);
    assert(set_parent_res.succeeded());
    TransformSystem::update(em);

    const auto& child_t_before = em.get_component<TransformComponent>(child);
    const glm::vec3 expected_world_pos = child_t_before.world_position; // 200 + 10*2 = 220, 100 + 20*2 = 140
    assert(approx_equal_vec3(expected_world_pos, glm::vec3(220.0f, 140.0f, 0.0f)));

    // Unparent via ReparentEntityCommand with new_parent = 0
    auto cmd = std::make_unique<ReparentEntityCommand>(&em, child, 0);
    assert(cmd->is_valid());
    history.execute_command(std::move(cmd));

    assert(em.get_component<HierarchyComponent>(child).parent == 0);
    TransformSystem::update(em);
    const auto& unparented_child_t = em.get_component<TransformComponent>(child);
    assert(approx_equal_vec3(unparented_child_t.position, glm::vec3(220.0f, 140.0f, 0.0f)));
    assert(approx_equal_vec3(unparented_child_t.world_position, glm::vec3(220.0f, 140.0f, 0.0f)));

    // Undo unparenting
    history.undo();
    assert(em.get_component<HierarchyComponent>(child).parent == parent);
    TransformSystem::update(em);
    const auto& undone_t = em.get_component<TransformComponent>(child);
    assert(approx_equal_vec3(undone_t.position, glm::vec3(10.0f, 20.0f, 0.0f)));
    assert(approx_equal_vec3(undone_t.world_position, glm::vec3(220.0f, 140.0f, 0.0f)));

    std::cout << "test_unparent_preserves_world_transform passed." << std::endl;
}

void test_non_uniform_and_negative_scale() {
    EntityManager em;
    EditorCommandHistory history(100);

    // Parent with non-uniform and negative scale (e.g. flipped horizontally)
    EntityID parent = em.create_entity();
    em.add_component<TagComponent>(parent, TagComponent{"FlippedParent"});
    em.add_component<TransformComponent>(parent, TransformComponent{
        glm::vec3(50.0f, 80.0f, 0.0f),
        glm::vec2(-1.5f, 2.0f), // negative scale on X
        30.0f
    });

    // Child in world space
    EntityID child = em.create_entity();
    em.add_component<TagComponent>(child, TagComponent{"Child"});
    em.add_component<TransformComponent>(child, TransformComponent{
        glm::vec3(120.0f, 200.0f, 0.0f),
        glm::vec2(0.5f, 0.5f),
        -15.0f
    });

    TransformSystem::update(em);

    const auto child_world_before = em.get_component<TransformComponent>(child);

    // Reparent under flipped parent
    auto cmd = std::make_unique<ReparentEntityCommand>(&em, child, parent);
    assert(cmd->is_valid());
    history.execute_command(std::move(cmd));

    TransformSystem::update(em);
    const auto child_world_after = em.get_component<TransformComponent>(child);

    assert(approx_equal_vec3(child_world_after.world_position, child_world_before.world_position));
    assert(approx_equal_vec2(child_world_after.world_scale, child_world_before.world_scale));
    assert(approx_equal(child_world_after.world_rotation, child_world_before.world_rotation));

    // Undo
    history.undo();
    TransformSystem::update(em);
    const auto child_world_undone = em.get_component<TransformComponent>(child);
    assert(approx_equal_vec3(child_world_undone.world_position, child_world_before.world_position));
    assert(approx_equal_vec2(child_world_undone.world_scale, child_world_before.world_scale));

    std::cout << "test_non_uniform_and_negative_scale passed." << std::endl;
}

void test_hierarchy_cycle_prevention_no_history() {
    EntityManager em;
    EditorCommandHistory history(100);

    // A -> B -> C
    EntityID a = em.create_entity();
    em.add_component<TransformComponent>(a, TransformComponent{glm::vec3(0.0f)});
    EntityID b = em.create_entity();
    em.add_component<TransformComponent>(b, TransformComponent{glm::vec3(10.0f)});
    EntityID c = em.create_entity();
    em.add_component<TransformComponent>(c, TransformComponent{glm::vec3(20.0f)});

    assert(em.set_parent(b, a).succeeded());
    assert(em.set_parent(c, b).succeeded());

    // Attempt to reparent A under C (would create cycle: A -> B -> C -> A)
    const size_t undo_count_before = history.get_undo_count();
    auto cmd = std::make_unique<ReparentEntityCommand>(&em, a, c);
    history.execute_command(std::move(cmd));

    // Command should fail execution and NOT be pushed to history
    assert(history.get_undo_count() == undo_count_before);
    assert(!history.can_undo());

    // Verify A still has no parent and B is still parented to A
    assert(em.get_component<HierarchyComponent>(a).parent == 0);
    assert(em.get_component<HierarchyComponent>(b).parent == a);
    assert(em.get_component<HierarchyComponent>(c).parent == b);

    std::cout << "test_hierarchy_cycle_prevention_no_history passed." << std::endl;
}

void test_self_parenting_rejection_no_history() {
    EntityManager em;
    EditorCommandHistory history(100);

    EntityID a = em.create_entity();
    em.add_component<TransformComponent>(a, TransformComponent{glm::vec3(0.0f)});

    // Attempt to parent A to A
    const size_t undo_count_before = history.get_undo_count();
    auto cmd = std::make_unique<ReparentEntityCommand>(&em, a, a);
    history.execute_command(std::move(cmd));

    assert(history.get_undo_count() == undo_count_before);
    assert(!history.can_undo());

    std::cout << "test_self_parenting_rejection_no_history passed." << std::endl;
}

void test_invalid_and_destroyed_parent_no_history() {
    EntityManager em;
    EditorCommandHistory history(100);

    EntityID a = em.create_entity();
    em.add_component<TransformComponent>(a, TransformComponent{glm::vec3(0.0f)});

    EntityID b = em.create_entity();
    em.add_component<TransformComponent>(b, TransformComponent{glm::vec3(10.0f)});
    em.destroy_entity(b);

    // Attempt to parent A to non-existent ID 999
    size_t undo_count_before = history.get_undo_count();
    auto cmd1 = std::make_unique<ReparentEntityCommand>(&em, a, 999);
    history.execute_command(std::move(cmd1));
    assert(history.get_undo_count() == undo_count_before);

    // Attempt to parent A to destroyed entity B
    auto cmd2 = std::make_unique<ReparentEntityCommand>(&em, a, b);
    history.execute_command(std::move(cmd2));
    assert(history.get_undo_count() == undo_count_before);

    std::cout << "test_invalid_and_destroyed_parent_no_history passed." << std::endl;
}

void test_missing_transform_rejection() {
    EntityManager em;
    EditorCommandHistory history(100);

    EntityID a = em.create_entity();
    em.add_component<TagComponent>(a, TagComponent{"EntityWithoutTransform"});

    EntityID b = em.create_entity();
    em.add_component<TransformComponent>(b, TransformComponent{glm::vec3(0.0f)});

    // Child without transform
    const auto res1 = em.set_parent(a, b);
    assert(!res1.succeeded());
    assert(res1.status == HierarchyOperationStatus::MissingTransform);

    // Parent without transform
    const auto res2 = em.set_parent(b, a);
    assert(!res2.succeeded());
    assert(res2.status == HierarchyOperationStatus::MissingTransform);

    // ReparentEntityCommand should fail and not push to history
    const size_t undo_count_before = history.get_undo_count();
    auto cmd = std::make_unique<ReparentEntityCommand>(&em, a, b);
    history.execute_command(std::move(cmd));
    assert(history.get_undo_count() == undo_count_before);

    std::cout << "test_missing_transform_rejection passed." << std::endl;
}

void test_degenerate_parent_scale_rejection() {
    EntityManager em;
    EditorCommandHistory history(100);

    // Parent with zero scale on X
    EntityID parent = em.create_entity();
    em.add_component<TransformComponent>(parent, TransformComponent{
        glm::vec3(0.0f),
        glm::vec2(0.0f, 1.0f)
    });

    EntityID child = em.create_entity();
    em.add_component<TransformComponent>(child, TransformComponent{glm::vec3(10.0f, 10.0f, 0.0f)});

    // 1. TransformSystem returns nullopt when parent scale is near zero (cannot invert)
    auto reparent_opt = TransformSystem::calculate_reparent_local_transform(em, child, parent);
    assert(!reparent_opt.has_value());

    // 2. ReparentEntityCommand fails validation and is rejected from command history
    const size_t undo_count_before = history.get_undo_count();
    auto cmd = std::make_unique<ReparentEntityCommand>(&em, child, parent);
    assert(!cmd->is_valid());
    history.execute_command(std::move(cmd));
    assert(history.get_undo_count() == undo_count_before);
    assert(!history.can_undo());

    // 3. Low-level EntityManager permits runtime structural reparenting even with zero scale
    const auto res = em.set_parent(child, parent);
    assert(res.succeeded());
    assert(res.status == HierarchyOperationStatus::Changed);

    std::cout << "test_degenerate_parent_scale_rejection passed." << std::endl;
}

void test_group_entities_centroid_and_reparent() {
    EntityManager em;
    EditorCommandHistory history(100);
    auto selection = std::make_shared<EditorSelection>();

    EntityID e1 = em.create_entity();
    em.add_component<TagComponent>(e1, TagComponent{"E1"});
    em.add_component<TransformComponent>(e1, TransformComponent{glm::vec3(0.0f, 0.0f, 0.0f)});

    EntityID e2 = em.create_entity();
    em.add_component<TagComponent>(e2, TagComponent{"E2"});
    em.add_component<TransformComponent>(e2, TransformComponent{glm::vec3(100.0f, 50.0f, 0.0f)});

    TransformSystem::update(em);
    selection->set_selection({e1, e2});

    auto cmd = std::make_unique<GroupSelectedEntitiesCommand>(&em, selection, std::vector<EntityID>{e1, e2}, "MyGroup");
    assert(cmd->is_valid());
    history.execute_command(std::move(cmd));

    // Centroid should be (50, 25, 0)
    assert(selection->count() == 1);
    EntityID group_id = selection->get_primary();
    assert(group_id != 0 && em.is_alive(group_id));
    assert(em.has_component<GroupComponent>(group_id));
    assert(em.get_component<TagComponent>(group_id).tag == "MyGroup");

    const auto& group_t = em.get_component<TransformComponent>(group_id);
    assert(approx_equal_vec3(group_t.position, glm::vec3(50.0f, 25.0f, 0.0f)));

    // Verify children
    assert(em.has_component<HierarchyComponent>(e1));
    assert(em.get_component<HierarchyComponent>(e1).parent == group_id);
    assert(em.has_component<HierarchyComponent>(e2));
    assert(em.get_component<HierarchyComponent>(e2).parent == group_id);

    // Verify world transforms preserved
    TransformSystem::update(em);
    assert(approx_equal_vec3(em.get_component<TransformComponent>(e1).world_position, glm::vec3(0.0f, 0.0f, 0.0f)));
    assert(approx_equal_vec3(em.get_component<TransformComponent>(e2).world_position, glm::vec3(100.0f, 50.0f, 0.0f)));

    std::cout << "test_group_entities_centroid_and_reparent passed." << std::endl;
}

void test_group_under_common_parent_with_scale_and_rotation() {
    EntityManager em;
    EditorCommandHistory history(100);

    EntityID parent = em.create_entity();
    em.add_component<TransformComponent>(parent, TransformComponent{
        glm::vec3(200.0f, 100.0f, 0.0f),
        glm::vec2(2.0f, 2.0f),
        90.0f
    });

    EntityID c1 = em.create_entity();
    em.add_component<TransformComponent>(c1, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});
    assert(em.set_parent(c1, parent).succeeded());

    EntityID c2 = em.create_entity();
    em.add_component<TransformComponent>(c2, TransformComponent{glm::vec3(20.0f, 0.0f, 0.0f)});
    assert(em.set_parent(c2, parent).succeeded());

    TransformSystem::update(em);
    const glm::vec3 w1_before = em.get_component<TransformComponent>(c1).world_position;
    const glm::vec3 w2_before = em.get_component<TransformComponent>(c2).world_position;

    auto cmd = std::make_unique<GroupSelectedEntitiesCommand>(&em, nullptr, std::vector<EntityID>{c1, c2});
    assert(cmd->is_valid());
    history.execute_command(std::move(cmd));

    EntityID group_id = em.get_component<HierarchyComponent>(c1).parent;
    assert(group_id != 0);
    // Group parent must be common parent
    assert(em.get_component<HierarchyComponent>(group_id).parent == parent);

    TransformSystem::update(em);
    assert(approx_equal_vec3(em.get_component<TransformComponent>(c1).world_position, w1_before));
    assert(approx_equal_vec3(em.get_component<TransformComponent>(c2).world_position, w2_before));

    std::cout << "test_group_under_common_parent_with_scale_and_rotation passed." << std::endl;
}

void test_group_sub_tree_filtering() {
    EntityManager em;
    EditorCommandHistory history(100);

    EntityID ancestor = em.create_entity();
    em.add_component<TransformComponent>(ancestor, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});

    EntityID child = em.create_entity();
    em.add_component<TransformComponent>(child, TransformComponent{glm::vec3(5.0f, 0.0f, 0.0f)});
    assert(em.set_parent(child, ancestor).succeeded());

    EntityID standalone = em.create_entity();
    em.add_component<TransformComponent>(standalone, TransformComponent{glm::vec3(50.0f, 0.0f, 0.0f)});

    TransformSystem::update(em);

    // Select ancestor, child, and standalone. Child should be filtered out from group roots.
    auto cmd = std::make_unique<GroupSelectedEntitiesCommand>(&em, nullptr, std::vector<EntityID>{ancestor, child, standalone});
    assert(cmd->is_valid());
    history.execute_command(std::move(cmd));

    EntityID group_id = em.get_component<HierarchyComponent>(ancestor).parent;
    assert(group_id != 0);
    assert(em.get_component<HierarchyComponent>(standalone).parent == group_id);
    // Child remains child of ancestor, NOT reparented to group directly
    assert(em.get_component<HierarchyComponent>(child).parent == ancestor);

    std::cout << "test_group_sub_tree_filtering passed." << std::endl;
}

void test_group_stable_entity_id_across_undo_redo() {
    EntityManager em;
    EditorCommandHistory history(100);

    EntityID e1 = em.create_entity();
    em.add_component<TransformComponent>(e1, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});
    EntityID e2 = em.create_entity();
    em.add_component<TransformComponent>(e2, TransformComponent{glm::vec3(20.0f, 0.0f, 0.0f)});

    auto cmd = std::make_unique<GroupSelectedEntitiesCommand>(&em, nullptr, std::vector<EntityID>{e1, e2});
    assert(cmd->is_valid());
    history.execute_command(std::move(cmd));

    EntityID group_id_first = em.get_component<HierarchyComponent>(e1).parent;
    assert(group_id_first != 0);

    // Undo
    history.undo();
    assert(!em.is_alive(group_id_first));
    assert(em.get_component<HierarchyComponent>(e1).parent == 0);

    // Redo -> group must be recreated with the EXACT same EntityID
    history.redo();
    assert(em.is_alive(group_id_first));
    assert(em.get_component<HierarchyComponent>(e1).parent == group_id_first);
    assert(em.get_component<HierarchyComponent>(e2).parent == group_id_first);

    std::cout << "test_group_stable_entity_id_across_undo_redo passed." << std::endl;
}

void test_group_selection_restored_on_undo_redo() {
    EntityManager em;
    EditorCommandHistory history(100);
    auto selection = std::make_shared<EditorSelection>();

    EntityID e1 = em.create_entity();
    em.add_component<TransformComponent>(e1, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});
    EntityID e2 = em.create_entity();
    em.add_component<TransformComponent>(e2, TransformComponent{glm::vec3(20.0f, 0.0f, 0.0f)});

    selection->set_selection({e1, e2});

    auto cmd = std::make_unique<GroupSelectedEntitiesCommand>(&em, selection, std::vector<EntityID>{e1, e2});
    history.execute_command(std::move(cmd));

    // After execute: selection is group entity
    assert(selection->count() == 1);
    EntityID group_id = selection->get_primary();
    assert(group_id != e1 && group_id != e2);

    // After undo: selection is {e1, e2}
    history.undo();
    assert(selection->count() == 2);
    assert(selection->is_selected(e1) && selection->is_selected(e2));

    // After redo: selection is group entity again
    history.redo();
    assert(selection->count() == 1);
    assert(selection->get_primary() == group_id);

    std::cout << "test_group_selection_restored_on_undo_redo passed." << std::endl;
}

void test_group_atomic_preflight_rejection() {
    EntityManager em;
    EditorCommandHistory history(100);

    // Entity with zero scale parent
    EntityID parent = em.create_entity();
    em.add_component<TransformComponent>(parent, TransformComponent{glm::vec3(0.0f), glm::vec2(0.0f, 1.0f)});

    EntityID e1 = em.create_entity();
    em.add_component<TransformComponent>(e1, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});
    assert(em.set_parent(e1, parent).succeeded());

    auto cmd = std::make_unique<GroupSelectedEntitiesCommand>(&em, nullptr, std::vector<EntityID>{e1});
    // Preflight must reject degenerate parent scale
    assert(!cmd->is_valid());
    history.execute_command(std::move(cmd));
    assert(!history.can_undo());

    std::cout << "test_group_atomic_preflight_rejection passed." << std::endl;
}

void test_ungroup_entities_reparents_to_parent_and_destroys_group() {
    EntityManager em;
    EditorCommandHistory history(100);
    auto selection = std::make_shared<EditorSelection>();

    EntityID parent = em.create_entity();
    em.add_component<TransformComponent>(parent, TransformComponent{glm::vec3(100.0f, 50.0f, 0.0f)});

    EntityID group = em.create_entity();
    em.add_component<GroupComponent>(group, GroupComponent{});
    em.add_component<TagComponent>(group, TagComponent{"GroupContainer"});
    em.add_component<TransformComponent>(group, TransformComponent{glm::vec3(20.0f, 10.0f, 0.0f)});
    assert(em.set_parent(group, parent).succeeded());

    EntityID child = em.create_entity();
    em.add_component<TransformComponent>(child, TransformComponent{glm::vec3(5.0f, 5.0f, 0.0f)});
    assert(em.set_parent(child, group).succeeded());

    TransformSystem::update(em);
    const glm::vec3 child_world_before = em.get_component<TransformComponent>(child).world_position;

    selection->select(group, false);

    auto cmd = std::make_unique<UngroupEntitiesCommand>(&em, selection, std::vector<EntityID>{group});
    assert(cmd->is_valid());
    history.execute_command(std::move(cmd));

    // Group is destroyed
    assert(!em.is_alive(group));

    // Child is now directly under parent
    assert(em.has_component<HierarchyComponent>(child));
    assert(em.get_component<HierarchyComponent>(child).parent == parent);

    // World transform preserved
    TransformSystem::update(em);
    assert(approx_equal_vec3(em.get_component<TransformComponent>(child).world_position, child_world_before));

    // Selection points to unpacked child
    assert(selection->count() == 1);
    assert(selection->is_selected(child));

    // Undo -> group is restored, child is child of group again
    history.undo();
    assert(em.is_alive(group));
    assert(em.get_component<HierarchyComponent>(group).parent == parent);
    assert(em.get_component<HierarchyComponent>(child).parent == group);
    assert(selection->is_selected(group));

    std::cout << "test_ungroup_entities_reparents_to_parent_and_destroys_group passed." << std::endl;
}

void test_ungroup_stable_entity_id_across_undo_redo() {
    EntityManager em;
    EditorCommandHistory history(100);

    EntityID group = em.create_entity();
    em.add_component<GroupComponent>(group, GroupComponent{});
    em.add_component<TransformComponent>(group, TransformComponent{glm::vec3(0.0f)});

    EntityID child = em.create_entity();
    em.add_component<TransformComponent>(child, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});
    assert(em.set_parent(child, group).succeeded());

    const EntityID original_group_id = group;

    auto cmd = std::make_unique<UngroupEntitiesCommand>(&em, nullptr, std::vector<EntityID>{group});
    assert(cmd->is_valid());
    history.execute_command(std::move(cmd));

    assert(!em.is_alive(original_group_id));

    // Undo restores with same ID
    history.undo();
    assert(em.is_alive(original_group_id));
    assert(em.has_component<GroupComponent>(original_group_id));
    assert(em.get_component<HierarchyComponent>(child).parent == original_group_id);

    std::cout << "test_ungroup_stable_entity_id_across_undo_redo passed." << std::endl;
}

void test_ungroup_rejects_non_group_entities() {
    EntityManager em;
    EditorCommandHistory history(100);

    EntityID parent = em.create_entity();
    em.add_component<TagComponent>(parent, TagComponent{"NormalParentWithChildren"});
    em.add_component<TransformComponent>(parent, TransformComponent{glm::vec3(0.0f)});
    // NOTE: NO GroupComponent

    EntityID child = em.create_entity();
    em.add_component<TransformComponent>(child, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});
    assert(em.set_parent(child, parent).succeeded());

    auto cmd = std::make_unique<UngroupEntitiesCommand>(&em, nullptr, std::vector<EntityID>{parent});
    // Must be rejected because parent does not have GroupComponent
    assert(!cmd->is_valid());
    history.execute_command(std::move(cmd));
    assert(!history.can_undo());
    assert(em.is_alive(parent));

    std::cout << "test_ungroup_rejects_non_group_entities passed." << std::endl;
}

void test_ungroup_multiple_and_nested_groups() {
    EntityManager em;
    EditorCommandHistory history(100);

    EntityID g1 = em.create_entity();
    em.add_component<GroupComponent>(g1, GroupComponent{});
    em.add_component<TransformComponent>(g1, TransformComponent{glm::vec3(0.0f)});

    EntityID g2 = em.create_entity();
    em.add_component<GroupComponent>(g2, GroupComponent{});
    em.add_component<TransformComponent>(g2, TransformComponent{glm::vec3(50.0f, 0.0f, 0.0f)});
    assert(em.set_parent(g2, g1).succeeded()); // G2 is child of G1

    EntityID c = em.create_entity();
    em.add_component<TransformComponent>(c, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});
    assert(em.set_parent(c, g2).succeeded());

    TransformSystem::update(em);

    // Select both G1 and G2. Preflight should only ungroup the root G1.
    auto cmd = std::make_unique<UngroupEntitiesCommand>(&em, nullptr, std::vector<EntityID>{g1, g2});
    assert(cmd->is_valid());
    history.execute_command(std::move(cmd));

    assert(!em.is_alive(g1));
    assert(em.is_alive(g2)); // G2 is unpacked as a root group now
    assert(em.get_component<HierarchyComponent>(g2).parent == 0);
    assert(em.get_component<HierarchyComponent>(c).parent == g2);

    std::cout << "test_ungroup_multiple_and_nested_groups passed." << std::endl;
}

void test_ungroup_redo_cycle() {
    EntityManager em;
    EditorCommandHistory history(100);
    auto selection = std::make_shared<EditorSelection>();

    EntityID group = em.create_entity();
    em.add_component<GroupComponent>(group, GroupComponent{});
    em.add_component<TagComponent>(group, TagComponent{"TestGroup"});
    em.add_component<TransformComponent>(group, TransformComponent{glm::vec3(50.0f, 50.0f, 0.0f)});

    EntityID c1 = em.create_entity();
    em.add_component<TransformComponent>(c1, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});
    assert(em.set_parent(c1, group).succeeded());

    EntityID c2 = em.create_entity();
    em.add_component<TransformComponent>(c2, TransformComponent{glm::vec3(20.0f, 0.0f, 0.0f)});
    assert(em.set_parent(c2, group).succeeded());

    selection->select(group, false);

    auto cmd = std::make_unique<UngroupEntitiesCommand>(&em, selection, std::vector<EntityID>{group});
    history.execute_command(std::move(cmd));

    // After execute: group destroyed, children unpacked
    assert(!em.is_alive(group));
    assert(selection->count() == 2);
    assert(selection->is_selected(c1) && selection->is_selected(c2));

    // After undo: group alive, children under group, selection on group
    history.undo();
    assert(em.is_alive(group));
    assert(em.get_component<HierarchyComponent>(c1).parent == group);
    assert(em.get_component<HierarchyComponent>(c2).parent == group);
    assert(selection->is_selected(group));

    // After redo: group destroyed again, children unpacked, selection on children
    history.redo();
    assert(!em.is_alive(group));
    assert(em.get_component<HierarchyComponent>(c1).parent == 0);
    assert(em.get_component<HierarchyComponent>(c2).parent == 0);
    assert(selection->count() == 2);
    assert(selection->is_selected(c1) && selection->is_selected(c2));

    std::cout << "test_ungroup_redo_cycle passed." << std::endl;
}

void test_ungroup_multiple_and_nested_groups_undo_redo() {
    EntityManager em;
    EditorCommandHistory history(100);

    EntityID g1 = em.create_entity();
    em.add_component<GroupComponent>(g1, GroupComponent{});
    em.add_component<TransformComponent>(g1, TransformComponent{glm::vec3(0.0f)});

    EntityID g2 = em.create_entity();
    em.add_component<GroupComponent>(g2, GroupComponent{});
    em.add_component<TransformComponent>(g2, TransformComponent{glm::vec3(50.0f, 0.0f, 0.0f)});
    assert(em.set_parent(g2, g1).succeeded());

    EntityID c = em.create_entity();
    em.add_component<TransformComponent>(c, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});
    assert(em.set_parent(c, g2).succeeded());

    auto cmd = std::make_unique<UngroupEntitiesCommand>(&em, nullptr, std::vector<EntityID>{g1, g2});
    history.execute_command(std::move(cmd));

    assert(!em.is_alive(g1));
    assert(em.is_alive(g2));
    assert(em.get_component<HierarchyComponent>(g2).parent == 0);

    // Undo: g1 restored, g2 child of g1
    history.undo();
    assert(em.is_alive(g1));
    assert(em.is_alive(g2));
    assert(em.get_component<HierarchyComponent>(g2).parent == g1);
    assert(em.get_component<HierarchyComponent>(c).parent == g2);

    // Redo: g1 destroyed again, g2 root
    history.redo();
    assert(!em.is_alive(g1));
    assert(em.is_alive(g2));
    assert(em.get_component<HierarchyComponent>(g2).parent == 0);
    assert(em.get_component<HierarchyComponent>(c).parent == g2);

    std::cout << "test_ungroup_multiple_and_nested_groups_undo_redo passed." << std::endl;
}

void test_group_execution_failure_no_partial_mutation() {
    EntityManager em;
    EditorCommandHistory history(100);

    EntityID c1 = em.create_entity();
    em.add_component<TransformComponent>(c1, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});

    EntityID c2 = em.create_entity();
    em.add_component<TransformComponent>(c2, TransformComponent{glm::vec3(20.0f, 0.0f, 0.0f)});

    auto cmd = std::make_unique<GroupSelectedEntitiesCommand>(&em, nullptr, std::vector<EntityID>{c1, c2});
    assert(cmd->is_valid());

    // Destroy c2 right before execute() to force execute() failure
    em.destroy_entity(c2);

    const bool executed = cmd->execute();
    assert(!executed);

    // Verify c1 was rolled back and is not parented to a dangling group
    assert(em.is_alive(c1));
    if (em.has_component<HierarchyComponent>(c1)) {
        assert(em.get_component<HierarchyComponent>(c1).parent == 0);
    }

    std::cout << "test_group_execution_failure_no_partial_mutation passed." << std::endl;
}

int main() {
    test_reparent_preserves_world_transform();
    test_unparent_preserves_world_transform();
    test_non_uniform_and_negative_scale();
    test_hierarchy_cycle_prevention_no_history();
    test_self_parenting_rejection_no_history();
    test_invalid_and_destroyed_parent_no_history();
    test_missing_transform_rejection();
    test_degenerate_parent_scale_rejection();
    test_group_entities_centroid_and_reparent();
    test_group_under_common_parent_with_scale_and_rotation();
    test_group_sub_tree_filtering();
    test_group_stable_entity_id_across_undo_redo();
    test_group_selection_restored_on_undo_redo();
    test_group_atomic_preflight_rejection();
    test_ungroup_entities_reparents_to_parent_and_destroys_group();
    test_ungroup_stable_entity_id_across_undo_redo();
    test_ungroup_rejects_non_group_entities();
    test_ungroup_multiple_and_nested_groups();
    test_ungroup_redo_cycle();
    test_ungroup_multiple_and_nested_groups_undo_redo();
    test_group_execution_failure_no_partial_mutation();

    std::cout << "All HierarchyCommandTests passed successfully!" << std::endl;
    return 0;
}
