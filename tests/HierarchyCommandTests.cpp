#include "core/Log.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/TransformSystem.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "ecs/components/PhysicsComponents.hpp"
#include "editor/commands/EditorCommandHistory.hpp"
#include "editor/commands/HierarchyCommands.hpp"
#include "editor/state/EditorEntityStateStore.hpp"
#include "editor/tools/EditorHierarchyFilter.hpp"
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

void test_hierarchy_lock_validation() {
    EntityManager em;
    EditorEntityStateStore store;

    EntityID parent = em.create_entity();
    em.add_component<TransformComponent>(parent, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});

    EntityID child = em.create_entity();
    em.add_component<TransformComponent>(child, TransformComponent{glm::vec3(20.0f, 0.0f, 0.0f)});

    // Child locked -> Reparent invalid
    store.set_locked(child, true);
    auto reparent_cmd1 = std::make_unique<ReparentEntityCommand>(&em, child, parent, nullptr, "", &store);
    assert(!reparent_cmd1->is_valid());

    // Unlock child, lock parent -> Reparent invalid
    store.set_locked(child, false);
    store.set_locked(parent, true);
    auto reparent_cmd2 = std::make_unique<ReparentEntityCommand>(&em, child, parent, nullptr, "", &store);
    assert(!reparent_cmd2->is_valid());

    // Both unlocked -> Reparent valid
    store.set_locked(parent, false);
    auto reparent_cmd3 = std::make_unique<ReparentEntityCommand>(&em, child, parent, nullptr, "", &store);
    assert(reparent_cmd3->is_valid());

    // Grouping with locked entity
    EntityID e1 = em.create_entity();
    em.add_component<TransformComponent>(e1, TransformComponent{glm::vec3(0.0f, 0.0f, 0.0f)});
    EntityID e2 = em.create_entity();
    em.add_component<TransformComponent>(e2, TransformComponent{glm::vec3(5.0f, 0.0f, 0.0f)});

    store.set_locked(e1, true);
    auto group_cmd = std::make_unique<GroupSelectedEntitiesCommand>(&em, nullptr, std::vector<EntityID>{e1, e2}, "Group", &store);
    assert(!group_cmd->is_valid());

    store.set_locked(e1, false);
    auto group_cmd_valid = std::make_unique<GroupSelectedEntitiesCommand>(&em, nullptr, std::vector<EntityID>{e1, e2}, "Group", &store);
    assert(group_cmd_valid->is_valid());
    assert(group_cmd_valid->execute());

    EntityID group_id = group_cmd_valid->get_group_entity();
    assert(group_id != 0);

    // Ungrouping locked group
    store.set_locked(group_id, true);
    auto ungroup_cmd = std::make_unique<UngroupEntitiesCommand>(&em, nullptr, std::vector<EntityID>{group_id}, &store);
    assert(!ungroup_cmd->is_valid());

    store.set_locked(group_id, false);
    auto ungroup_cmd_valid = std::make_unique<UngroupEntitiesCommand>(&em, nullptr, std::vector<EntityID>{group_id}, &store);
    assert(ungroup_cmd_valid->is_valid());
    assert(ungroup_cmd_valid->execute());

    std::cout << "test_hierarchy_lock_validation passed." << std::endl;
}

void test_delete_blocks_locked_descendants_and_restores_metadata() {
    EntityManager em;
    EditorEntityStateStore store;
    EditorCommandHistory history;

    EntityID parent = em.create_entity();
    em.add_component<TagComponent>(parent, TagComponent{"Parent"});
    em.add_component<TransformComponent>(parent, TransformComponent{});

    EntityID child = em.create_entity();
    em.add_component<TagComponent>(child, TagComponent{"Child"});
    em.add_component<TransformComponent>(child, TransformComponent{});
    assert(em.set_parent(child, parent).succeeded());

    // 1. Child is locked -> attempting to delete parent must fail atomically
    store.set_locked(child, true);
    auto delete_locked_child_cmd = std::make_unique<DeleteEntitiesCommand>(&em, nullptr, std::vector<EntityID>{parent}, &store);
    assert(!delete_locked_child_cmd->is_valid());

    // 2. Child is unlocked, parent and child are hidden in editor
    store.set_locked(child, false);
    store.set_visible(parent, false);
    store.set_visible(child, false);
    assert(!store.is_visible(parent));
    assert(!store.is_visible(child));

    // Execute Delete on both parent and child
    auto delete_cmd = std::make_unique<DeleteEntitiesCommand>(&em, nullptr, std::vector<EntityID>{parent, child}, &store);
    assert(delete_cmd->is_valid());
    history.execute_command(std::move(delete_cmd));

    assert(!em.is_alive(parent));
    assert(!em.is_alive(child));
    assert(store.is_visible(parent)); // Reset/removed from store upon delete
    assert(store.is_visible(child));

    // 3. Undo Delete -> entities and editor metadata are restored
    assert(history.can_undo());
    history.undo();
    assert(em.is_alive(parent));
    assert(em.is_alive(child));
    assert(em.get_component<HierarchyComponent>(child).parent == parent);
    assert(!store.is_visible(parent)); // Metadata accurately restored!
    assert(!store.is_visible(child));

    // 4. Redo Delete
    assert(history.can_redo());
    history.redo();
    assert(!em.is_alive(parent));
    assert(!em.is_alive(child));

    std::cout << "test_delete_blocks_locked_descendants_and_restores_metadata passed." << std::endl;
}

void test_delete_parent_recursively_deletes_entire_subtree_and_restores_on_undo() {
    EntityManager em;
    EditorCommandHistory history(100);
    EditorEntityStateStore store;
    auto selection = std::make_shared<EditorSelection>();

    EntityID parent = em.create_entity();
    em.add_component<TransformComponent>(parent, TransformComponent{glm::vec3(0.0f)});
    em.add_component<TagComponent>(parent, TagComponent{"Parent"});

    EntityID child = em.create_entity();
    em.add_component<TransformComponent>(child, TransformComponent{glm::vec3(10.0f)});
    em.add_component<TagComponent>(child, TagComponent{"Child"});
    assert(em.set_parent(child, parent).succeeded());

    EntityID grandchild = em.create_entity();
    em.add_component<TransformComponent>(grandchild, TransformComponent{glm::vec3(20.0f)});
    em.add_component<TagComponent>(grandchild, TagComponent{"Grandchild"});
    assert(em.set_parent(grandchild, child).succeeded());

    // Mark child as hidden in editor
    store.set_visible(child, false);
    assert(!store.is_visible(child));

    // Delete the parent node
    history.execute_command(std::make_unique<DeleteEntitiesCommand>(&em, selection, std::vector<EntityID>{parent}, &store));

    // Parent, child and grandchild are ALL recursively destroyed
    assert(!em.is_alive(parent));
    assert(!em.is_alive(child));
    assert(!em.is_alive(grandchild));

    // Undo delete
    assert(history.can_undo());
    history.undo();

    // All are restored with intact hierarchy connections and metadata
    assert(em.is_alive(parent));
    assert(em.is_alive(child));
    assert(em.is_alive(grandchild));
    assert(em.get_component<HierarchyComponent>(child).parent == parent);
    assert(em.get_component<HierarchyComponent>(grandchild).parent == child);
    assert(!store.is_visible(child)); // Metadata intact!

    // Redo delete
    assert(history.can_redo());
    history.redo();
    assert(!em.is_alive(parent));
    assert(!em.is_alive(child));
    assert(!em.is_alive(grandchild));

    std::cout << "test_delete_parent_recursively_deletes_entire_subtree_and_restores_on_undo passed." << std::endl;
}

void test_reorder_children_under_parent() {
    EntityManager em;
    EditorCommandHistory history(100);

    EntityID parent = em.create_entity();
    em.add_component<TransformComponent>(parent, TransformComponent{glm::vec3(0.0f)});

    EntityID c0 = em.create_entity();
    em.add_component<TransformComponent>(c0, TransformComponent{glm::vec3(1.0f)});
    assert(em.set_parent(c0, parent).succeeded());

    EntityID c1 = em.create_entity();
    em.add_component<TransformComponent>(c1, TransformComponent{glm::vec3(2.0f)});
    assert(em.set_parent(c1, parent).succeeded());

    EntityID c2 = em.create_entity();
    em.add_component<TransformComponent>(c2, TransformComponent{glm::vec3(3.0f)});
    assert(em.set_parent(c2, parent).succeeded());

    // Initial order: [c0, c1, c2]
    assert(em.get_sibling_index(c0) == 0);
    assert(em.get_sibling_index(c1) == 1);
    assert(em.get_sibling_index(c2) == 2);
    assert(em.get_sibling_count(c0) == 3);

    // Reorder c0 to index 2: new order [c1, c2, c0]
    history.execute_command(std::make_unique<ReorderEntityCommand>(&em, c0, 2));
    const auto& children1 = em.get_component<HierarchyComponent>(parent).children;
    assert(children1.size() == 3);
    assert(children1[0] == c1);
    assert(children1[1] == c2);
    assert(children1[2] == c0);

    // Undo: restores [c0, c1, c2]
    assert(history.can_undo());
    history.undo();
    const auto& children_undo = em.get_component<HierarchyComponent>(parent).children;
    assert(children_undo[0] == c0);
    assert(children_undo[1] == c1);
    assert(children_undo[2] == c2);

    // Redo: [c1, c2, c0]
    assert(history.can_redo());
    history.redo();
    const auto& children_redo = em.get_component<HierarchyComponent>(parent).children;
    assert(children_redo[0] == c1);
    assert(children_redo[1] == c2);
    assert(children_redo[2] == c0);

    std::cout << "test_reorder_children_under_parent passed." << std::endl;
}

void test_reorder_root_entities() {
    EntityManager em;
    EditorCommandHistory history(100);

    EntityID r0 = em.create_entity();
    em.add_component<TransformComponent>(r0, TransformComponent{glm::vec3(0.0f)});

    EntityID r1 = em.create_entity();
    em.add_component<TransformComponent>(r1, TransformComponent{glm::vec3(10.0f)});

    EntityID r2 = em.create_entity();
    em.add_component<TransformComponent>(r2, TransformComponent{glm::vec3(20.0f)});

    // Verify that living_entities vector is UNTOUCHED (ECS lifecycle stability)
    const auto& living_before = em.get_living_entities();
    const std::vector<EntityID> expected_living = {r0, r1, r2};
    assert(living_before == expected_living);

    // Move r2 to index 0 (top): order [r2, r0, r1]
    history.execute_command(std::make_unique<ReorderEntityCommand>(&em, r2, 0));
    assert(em.get_sibling_index(r2) == 0);
    assert(em.get_sibling_index(r0) == 1);
    assert(em.get_sibling_index(r1) == 2);
    assert(em.get_root_entities() == (std::vector<EntityID>{r2, r0, r1}));

    // Crucial architectural assertion: m_living_entities did NOT change!
    assert(em.get_living_entities() == expected_living);

    // Undo: [r0, r1, r2]
    assert(history.can_undo());
    history.undo();
    assert(em.get_sibling_index(r0) == 0);
    assert(em.get_sibling_index(r1) == 1);
    assert(em.get_sibling_index(r2) == 2);
    assert(em.get_root_entities() == expected_living);
    assert(em.get_living_entities() == expected_living);

    // Redo: [r2, r0, r1]
    assert(history.can_redo());
    history.redo();
    assert(em.get_sibling_index(r2) == 0);
    assert(em.get_sibling_index(r0) == 1);
    assert(em.get_sibling_index(r1) == 2);
    assert(em.get_root_entities() == (std::vector<EntityID>{r2, r0, r1}));
    assert(em.get_living_entities() == expected_living);

    std::cout << "test_reorder_root_entities passed." << std::endl;
}

void test_reorder_rejects_when_parent_changed_externally() {
    EntityManager em;

    EntityID parentA = em.create_entity();
    em.add_component<TransformComponent>(parentA, TransformComponent{glm::vec3(0.0f)});

    EntityID parentB = em.create_entity();
    em.add_component<TransformComponent>(parentB, TransformComponent{glm::vec3(10.0f)});

    EntityID child = em.create_entity();
    em.add_component<TransformComponent>(child, TransformComponent{glm::vec3(1.0f)});
    assert(em.set_parent(child, parentA).succeeded());

    // Create command expecting parentA
    ReorderEntityCommand cmd(&em, child, 0);

    // Externally change parent to parentB
    assert(em.set_parent(child, parentB).succeeded());

    // Command should reject execution because parent is now parentB
    assert(!cmd.execute());

    std::cout << "test_reorder_rejects_when_parent_changed_externally passed." << std::endl;
}

void test_reorder_locked_entity_rejection() {
    EntityManager em;
    EditorCommandHistory history(100);
    EditorEntityStateStore store;

    EntityID parent = em.create_entity();
    em.add_component<TransformComponent>(parent, TransformComponent{glm::vec3(0.0f)});

    EntityID c0 = em.create_entity();
    em.add_component<TransformComponent>(c0, TransformComponent{glm::vec3(1.0f)});
    assert(em.set_parent(c0, parent).succeeded());

    EntityID c1 = em.create_entity();
    em.add_component<TransformComponent>(c1, TransformComponent{glm::vec3(2.0f)});
    assert(em.set_parent(c1, parent).succeeded());

    // Lock c0
    store.set_locked(c0, true);

    // Attempt to reorder locked c0 -> rejected
    const size_t undo_count_before = history.get_undo_count();
    history.execute_command(std::make_unique<ReorderEntityCommand>(&em, c0, 1, nullptr, &store));
    assert(history.get_undo_count() == undo_count_before);
    assert(em.get_sibling_index(c0) == 0);

    // Unlock c0, lock parent -> rejected
    store.set_locked(c0, false);
    store.set_locked(parent, true);
    history.execute_command(std::make_unique<ReorderEntityCommand>(&em, c0, 1, nullptr, &store));
    assert(history.get_undo_count() == undo_count_before);
    assert(em.get_sibling_index(c0) == 0);

    std::cout << "test_reorder_locked_entity_rejection passed." << std::endl;
}

void test_reparent_with_target_index() {
    EntityManager em;
    EditorCommandHistory history(100);

    EntityID parent = em.create_entity();
    em.add_component<TransformComponent>(parent, TransformComponent{glm::vec3(0.0f)});

    EntityID c0 = em.create_entity();
    em.add_component<TransformComponent>(c0, TransformComponent{glm::vec3(1.0f)});
    assert(em.set_parent(c0, parent).succeeded());

    EntityID c1 = em.create_entity();
    em.add_component<TransformComponent>(c1, TransformComponent{glm::vec3(2.0f)});
    assert(em.set_parent(c1, parent).succeeded());

    EntityID outside = em.create_entity();
    em.add_component<TransformComponent>(outside, TransformComponent{glm::vec3(50.0f)});

    // Reparent `outside` into `parent` at index 1 (between c0 and c1)
    history.execute_command(std::make_unique<ReparentEntityCommand>(
        &em, outside, parent, nullptr, "", nullptr, std::make_optional<size_t>(1)
    ));

    const auto& children = em.get_component<HierarchyComponent>(parent).children;
    assert(children.size() == 3);
    assert(children[0] == c0);
    assert(children[1] == outside);
    assert(children[2] == c1);

    // Undo reparent
    assert(history.can_undo());
    history.undo();
    assert(em.get_component<HierarchyComponent>(outside).parent == 0);
    const auto& children_undone = em.get_component<HierarchyComponent>(parent).children;
    assert(children_undone.size() == 2);
    assert(children_undone[0] == c0);
    assert(children_undone[1] == c1);

    std::cout << "test_reparent_with_target_index passed." << std::endl;
}

void test_duplicate_subtree_hierarchy_remaps_children_and_preserves_transforms() {
    EntityManager em;
    EditorCommandHistory history(100);
    auto selection = std::make_shared<EditorSelection>();

    // Create hierarchy: Parent -> Child -> Grandchild
    EntityID parent = em.create_entity();
    em.add_component<TagComponent>(parent, TagComponent{"ParentNode"});
    em.add_component<TransformComponent>(parent, TransformComponent{glm::vec3(100.0f, 50.0f, 0.0f)});

    EntityID child = em.create_entity();
    em.add_component<TagComponent>(child, TagComponent{"ChildNode"});
    em.add_component<TransformComponent>(child, TransformComponent{glm::vec3(10.0f, 5.0f, 0.0f)});
    assert(em.set_parent(child, parent).succeeded());

    EntityID grandchild = em.create_entity();
    em.add_component<TagComponent>(grandchild, TagComponent{"GrandchildNode"});
    em.add_component<TransformComponent>(grandchild, TransformComponent{glm::vec3(2.0f, 3.0f, 0.0f)});
    assert(em.set_parent(grandchild, child).succeeded());

    TransformSystem::update(em);
    selection->select(parent);

    // Duplicate Parent (should duplicate the entire subtree: parent, child, grandchild)
    auto cmd = std::make_unique<DuplicateEntitiesCommand>(&em, selection, std::vector<EntityID>{parent});
    assert(cmd->is_valid());
    history.execute_command(std::move(cmd));

    // Selection must now contain only the newly duplicated root
    assert(selection->count() == 1);
    EntityID parent_copy = selection->get_primary();
    assert(parent_copy != parent);
    assert(em.is_alive(parent_copy));

    // Verify parent copy properties
    assert(em.get_component<TagComponent>(parent_copy).tag == "ParentNode_Copy");
    assert(em.get_component<TransformComponent>(parent_copy).position == glm::vec3(130.0f, 80.0f, 0.0f));

    // Verify child copy is parented to parent copy
    assert(em.has_component<HierarchyComponent>(parent_copy));
    const auto& parent_copy_hier = em.get_component<HierarchyComponent>(parent_copy);
    assert(parent_copy_hier.children.size() == 1);
    EntityID child_copy = parent_copy_hier.children[0];
    assert(child_copy != child && child_copy != parent_copy);
    assert(em.is_alive(child_copy));
    assert(em.get_component<HierarchyComponent>(child_copy).parent == parent_copy);
    assert(em.get_component<TransformComponent>(child_copy).position == glm::vec3(10.0f, 5.0f, 0.0f));

    // Verify grandchild copy is parented to child copy
    const auto& child_copy_hier = em.get_component<HierarchyComponent>(child_copy);
    assert(child_copy_hier.children.size() == 1);
    EntityID grandchild_copy = child_copy_hier.children[0];
    assert(grandchild_copy != grandchild && grandchild_copy != child_copy);
    assert(em.is_alive(grandchild_copy));
    assert(em.get_component<HierarchyComponent>(grandchild_copy).parent == child_copy);
    assert(em.get_component<TransformComponent>(grandchild_copy).position == glm::vec3(2.0f, 3.0f, 0.0f));

    // Verify Undo destroys all copies and restores selection to original parent
    history.undo();
    assert(!em.is_alive(parent_copy));
    assert(!em.is_alive(child_copy));
    assert(!em.is_alive(grandchild_copy));
    assert(selection->count() == 1 && selection->is_selected(parent));

    // Verify Redo restores all copies with identical parent-child links
    history.redo();
    assert(em.is_alive(parent_copy));
    assert(em.is_alive(child_copy));
    assert(em.is_alive(grandchild_copy));
    assert(em.get_component<HierarchyComponent>(child_copy).parent == parent_copy);
    assert(em.get_component<HierarchyComponent>(grandchild_copy).parent == child_copy);
    assert(selection->count() == 1 && selection->is_selected(parent_copy));

    std::cout << "test_duplicate_subtree_hierarchy_remaps_children_and_preserves_transforms passed." << std::endl;
}

void test_duplicate_locked_entity_or_subtree_aborts_without_mutation() {
    EntityManager em;
    EditorCommandHistory history(100);
    EditorEntityStateStore state_store;
    auto selection = std::make_shared<EditorSelection>();

    EntityID parent = em.create_entity();
    em.add_component<TransformComponent>(parent, TransformComponent{glm::vec3(0.0f)});

    EntityID child = em.create_entity();
    em.add_component<TransformComponent>(child, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});
    assert(em.set_parent(child, parent).succeeded());

    // Lock the child entity
    state_store.set_locked(child, true);

    // Duplicating parent must fail preflight check and leave state untouched
    auto cmd = std::make_unique<DuplicateEntitiesCommand>(&em, selection, std::vector<EntityID>{parent}, &state_store);
    assert(!cmd->is_valid());
    history.execute_command(std::move(cmd));
    assert(!history.can_undo());

    std::cout << "test_duplicate_locked_entity_or_subtree_aborts_without_mutation passed." << std::endl;
}

void test_duplicate_subtree_under_rotated_scaled_external_parent_preserves_world_offset_and_relative_transforms() {
    EntityManager em;
    EditorCommandHistory history(100);
    auto selection = std::make_shared<EditorSelection>();

    // External unselected parent with translation, rotation and non-uniform scale
    EntityID external_parent = em.create_entity();
    em.add_component<TagComponent>(external_parent, TagComponent{"ExternalParent"});
    em.add_component<TransformComponent>(external_parent, TransformComponent{
        glm::vec3(200.0f, 100.0f, 0.0f),
        glm::vec2(2.0f, 0.5f),
        45.0f
    });

    // Root entity parented to external_parent
    EntityID root = em.create_entity();
    em.add_component<TagComponent>(root, TagComponent{"SubtreeRoot"});
    em.add_component<TransformComponent>(root, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});
    assert(em.set_parent(root, external_parent).succeeded());

    // Child entity parented to root
    EntityID child = em.create_entity();
    em.add_component<TagComponent>(child, TagComponent{"Child"});
    em.add_component<TransformComponent>(child, TransformComponent{glm::vec3(5.0f, 5.0f, 0.0f)});
    assert(em.set_parent(child, root).succeeded());

    // Grandchild entity parented to child
    EntityID grandchild = em.create_entity();
    em.add_component<TagComponent>(grandchild, TagComponent{"Grandchild"});
    em.add_component<TransformComponent>(grandchild, TransformComponent{glm::vec3(2.0f, -1.0f, 0.0f)});
    assert(em.set_parent(grandchild, child).succeeded());

    TransformSystem::update(em);
    const glm::vec3 root_world_before = em.get_component<TransformComponent>(root).world_position;
    const glm::vec3 child_world_before = em.get_component<TransformComponent>(child).world_position;
    const glm::vec3 grandchild_world_before = em.get_component<TransformComponent>(grandchild).world_position;

    // Relative world vector between root and child/grandchild
    const glm::vec3 child_rel_world = child_world_before - root_world_before;
    const glm::vec3 grandchild_rel_world = grandchild_world_before - root_world_before;

    selection->select(root);

    // Duplicate only the root (its subtree should be duplicated, preserving external parent)
    auto cmd = std::make_unique<DuplicateEntitiesCommand>(&em, selection, std::vector<EntityID>{root});
    assert(cmd->is_valid());
    history.execute_command(std::move(cmd));

    assert(selection->count() == 1);
    EntityID root_copy = selection->get_primary();
    assert(root_copy != root && em.is_alive(root_copy));

    TransformSystem::update(em);

    // 1. Root copy world position must be displaced by (+30, +30, 0) in world space
    const glm::vec3 root_copy_world = em.get_component<TransformComponent>(root_copy).world_position;
    assert(std::abs(root_copy_world.x - (root_world_before.x + 30.0f)) < 0.01f);
    assert(std::abs(root_copy_world.y - (root_world_before.y + 30.0f)) < 0.01f);

    // 2. Root copy is parented to the external parent
    assert(em.get_component<HierarchyComponent>(root_copy).parent == external_parent);

    // 3. Child copy is parented to root copy
    const auto& root_copy_children = em.get_component<HierarchyComponent>(root_copy).children;
    assert(root_copy_children.size() == 1);
    EntityID child_copy = root_copy_children[0];
    assert(em.get_component<HierarchyComponent>(child_copy).parent == root_copy);

    // 4. Grandchild copy is parented to child copy
    const auto& child_copy_children = em.get_component<HierarchyComponent>(child_copy).children;
    assert(child_copy_children.size() == 1);
    EntityID grandchild_copy = child_copy_children[0];
    assert(em.get_component<HierarchyComponent>(grandchild_copy).parent == child_copy);

    // 5. Child and grandchild maintain identical relative world offsets from the root copy
    const glm::vec3 child_copy_world = em.get_component<TransformComponent>(child_copy).world_position;
    const glm::vec3 grandchild_copy_world = em.get_component<TransformComponent>(grandchild_copy).world_position;
    assert(glm::distance(child_copy_world - root_copy_world, child_rel_world) < 0.01f);
    assert(glm::distance(grandchild_copy_world - root_copy_world, grandchild_rel_world) < 0.01f);

    // 6. Undo/Redo preserves exact world geometry and hierarchy
    history.undo();
    assert(!em.is_alive(root_copy) && !em.is_alive(child_copy) && !em.is_alive(grandchild_copy));

    history.redo();
    assert(em.is_alive(root_copy) && em.is_alive(child_copy) && em.is_alive(grandchild_copy));
    TransformSystem::update(em);
    assert(std::abs(em.get_component<TransformComponent>(root_copy).world_position.x - (root_world_before.x + 30.0f)) < 0.01f);
    assert(std::abs(em.get_component<TransformComponent>(root_copy).world_position.y - (root_world_before.y + 30.0f)) < 0.01f);

    std::cout << "test_duplicate_subtree_under_rotated_scaled_external_parent_preserves_world_offset_and_relative_transforms passed." << std::endl;
}

void test_reparent_multiple_entities_preserves_world_transforms_and_order() {
    EntityManager em;
    EditorCommandHistory history(100);
    auto selection = std::make_shared<EditorSelection>();

    EntityID parent = em.create_entity();
    em.add_component<TagComponent>(parent, TagComponent{"TargetParent"});
    em.add_component<TransformComponent>(parent, TransformComponent{
        glm::vec3(200.0f, 100.0f, 0.0f),
        glm::vec2(1.5f, 0.8f),
        30.0f
    });

    EntityID e1 = em.create_entity();
    em.add_component<TagComponent>(e1, TagComponent{"Entity1"});
    em.add_component<TransformComponent>(e1, TransformComponent{glm::vec3(10.0f, 20.0f, 0.0f)});

    EntityID e2 = em.create_entity();
    em.add_component<TagComponent>(e2, TagComponent{"Entity2"});
    em.add_component<TransformComponent>(e2, TransformComponent{glm::vec3(50.0f, 60.0f, 0.0f)});

    EntityID e3 = em.create_entity();
    em.add_component<TagComponent>(e3, TagComponent{"Entity3"});
    em.add_component<TransformComponent>(e3, TransformComponent{glm::vec3(100.0f, 120.0f, 0.0f)});

    TransformSystem::update(em);
    const glm::vec3 w1 = em.get_component<TransformComponent>(e1).world_position;
    const glm::vec3 w2 = em.get_component<TransformComponent>(e2).world_position;
    const glm::vec3 w3 = em.get_component<TransformComponent>(e3).world_position;

    selection->select(e1);
    selection->select(e2, true);
    selection->select(e3, true);

    auto cmd = std::make_unique<ReparentEntitiesCommand>(&em, std::vector<EntityID>{e1, e2, e3}, parent, selection, "", nullptr, 0);
    assert(cmd->is_valid());
    history.execute_command(std::move(cmd));

    TransformSystem::update(em);

    // Verify all 3 entities are now children of parent in order
    assert(em.has_component<HierarchyComponent>(parent));
    const auto& children = em.get_component<HierarchyComponent>(parent).children;
    assert(children.size() == 3);
    assert(children[0] == e1);
    assert(children[1] == e2);
    assert(children[2] == e3);

    // Verify world positions are preserved
    assert(glm::distance(em.get_component<TransformComponent>(e1).world_position, w1) < 0.01f);
    assert(glm::distance(em.get_component<TransformComponent>(e2).world_position, w2) < 0.01f);
    assert(glm::distance(em.get_component<TransformComponent>(e3).world_position, w3) < 0.01f);

    // Verify Undo restores them as roots with identical world transforms
    history.undo();
    TransformSystem::update(em);
    assert(!em.has_component<HierarchyComponent>(parent) || em.get_component<HierarchyComponent>(parent).children.empty());
    assert(glm::distance(em.get_component<TransformComponent>(e1).world_position, w1) < 0.01f);
    assert(glm::distance(em.get_component<TransformComponent>(e2).world_position, w2) < 0.01f);
    assert(glm::distance(em.get_component<TransformComponent>(e3).world_position, w3) < 0.01f);

    // Verify Redo restores parent-child relationships and transforms
    history.redo();
    TransformSystem::update(em);
    const auto& children_redone = em.get_component<HierarchyComponent>(parent).children;
    assert(children_redone.size() == 3);
    assert(children_redone[0] == e1);
    assert(children_redone[1] == e2);
    assert(children_redone[2] == e3);
    assert(glm::distance(em.get_component<TransformComponent>(e1).world_position, w1) < 0.01f);
    assert(glm::distance(em.get_component<TransformComponent>(e2).world_position, w2) < 0.01f);
    assert(glm::distance(em.get_component<TransformComponent>(e3).world_position, w3) < 0.01f);

    std::cout << "test_reparent_multiple_entities_preserves_world_transforms_and_order passed." << std::endl;
}

void test_reparent_multiple_entities_with_cycle_rejection() {
    EntityManager em;
    EditorCommandHistory history(100);

    EntityID root1 = em.create_entity();
    em.add_component<TransformComponent>(root1, TransformComponent{glm::vec3(0.0f)});

    EntityID child1 = em.create_entity();
    em.add_component<TransformComponent>(child1, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});
    assert(em.set_parent(child1, root1).succeeded());

    EntityID grandchild1 = em.create_entity();
    em.add_component<TransformComponent>(grandchild1, TransformComponent{glm::vec3(20.0f, 0.0f, 0.0f)});
    assert(em.set_parent(grandchild1, child1).succeeded());

    EntityID other = em.create_entity();
    em.add_component<TransformComponent>(other, TransformComponent{glm::vec3(30.0f, 0.0f, 0.0f)});

    // Attempting to reparent {root1, other} into grandchild1 must fail preflight check
    auto cmd = std::make_unique<ReparentEntitiesCommand>(&em, std::vector<EntityID>{root1, other}, grandchild1);
    assert(!cmd->is_valid());
    history.execute_command(std::move(cmd));
    assert(!history.can_undo());

    std::cout << "test_reparent_multiple_entities_with_cycle_rejection passed." << std::endl;
}

void test_reparent_multiple_entities_with_locked_entity_rejection() {
    EntityManager em;
    EditorCommandHistory history(100);
    EditorEntityStateStore state_store;

    EntityID target = em.create_entity();
    em.add_component<TransformComponent>(target, TransformComponent{glm::vec3(0.0f)});

    EntityID e1 = em.create_entity();
    em.add_component<TransformComponent>(e1, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});

    EntityID e2 = em.create_entity();
    em.add_component<TransformComponent>(e2, TransformComponent{glm::vec3(20.0f, 0.0f, 0.0f)});

    state_store.set_locked(e2, true);

    auto cmd = std::make_unique<ReparentEntitiesCommand>(&em, std::vector<EntityID>{e1, e2}, target, nullptr, "", &state_store);
    assert(!cmd->is_valid());
    history.execute_command(std::move(cmd));
    assert(!history.can_undo());

    std::cout << "test_reparent_multiple_entities_with_locked_entity_rejection passed." << std::endl;
}

void test_reparent_multiple_entities_unparent_to_root() {
    EntityManager em;
    EditorCommandHistory history(100);
    auto selection = std::make_shared<EditorSelection>();

    EntityID parent = em.create_entity();
    em.add_component<TransformComponent>(parent, TransformComponent{glm::vec3(100.0f, 50.0f, 0.0f), glm::vec2(2.0f, 2.0f)});

    EntityID c1 = em.create_entity();
    em.add_component<TransformComponent>(c1, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});
    assert(em.set_parent(c1, parent).succeeded());

    EntityID c2 = em.create_entity();
    em.add_component<TransformComponent>(c2, TransformComponent{glm::vec3(20.0f, 0.0f, 0.0f)});
    assert(em.set_parent(c2, parent).succeeded());

    TransformSystem::update(em);
    const glm::vec3 w1 = em.get_component<TransformComponent>(c1).world_position;
    const glm::vec3 w2 = em.get_component<TransformComponent>(c2).world_position;

    auto cmd = std::make_unique<ReparentEntitiesCommand>(&em, std::vector<EntityID>{c1, c2}, 0, selection);
    assert(cmd->is_valid());
    history.execute_command(std::move(cmd));

    TransformSystem::update(em);
    assert(!em.has_component<HierarchyComponent>(c1) || em.get_component<HierarchyComponent>(c1).parent == 0);
    assert(!em.has_component<HierarchyComponent>(c2) || em.get_component<HierarchyComponent>(c2).parent == 0);
    assert(glm::distance(em.get_component<TransformComponent>(c1).world_position, w1) < 0.01f);
    assert(glm::distance(em.get_component<TransformComponent>(c2).world_position, w2) < 0.01f);

    history.undo();
    TransformSystem::update(em);
    assert(em.get_component<HierarchyComponent>(c1).parent == parent);
    assert(em.get_component<HierarchyComponent>(c2).parent == parent);
    assert(glm::distance(em.get_component<TransformComponent>(c1).world_position, w1) < 0.01f);
    assert(glm::distance(em.get_component<TransformComponent>(c2).world_position, w2) < 0.01f);

    std::cout << "test_reparent_multiple_entities_unparent_to_root passed." << std::endl;
}

void test_scene_hierarchy_smart_search_and_component_filters() {
    EntityManager em;
    auto state_store = std::make_shared<EditorEntityStateStore>();

    EntityID parent = em.create_entity();
    em.add_component<TransformComponent>(parent, TransformComponent{});
    em.add_component<TagComponent>(parent, TagComponent{"Enemies_Group"});

    EntityID goblin = em.create_entity();
    em.add_component<TransformComponent>(goblin, TransformComponent{});
    em.add_component<TagComponent>(goblin, TagComponent{"Goblin_Scout"});
    em.add_component<SpriteComponent>(goblin, SpriteComponent{});
    const auto res1 = em.set_parent(goblin, parent);
    assert(res1.status == HierarchyOperationStatus::Changed);

    EntityID boss = em.create_entity();
    em.add_component<TransformComponent>(boss, TransformComponent{});
    em.add_component<TagComponent>(boss, TagComponent{"Goblin_Boss"});
    em.add_component<RigidBodyComponent>(boss, RigidBodyComponent{});
    em.add_component<ColliderComponent>(boss, ColliderComponent{});
    const auto res2 = em.set_parent(boss, parent);
    assert(res2.status == HierarchyOperationStatus::Changed);

    EntityID camera = em.create_entity();
    em.add_component<TransformComponent>(camera, TransformComponent{});
    em.add_component<TagComponent>(camera, TagComponent{"Main_Camera"});
    em.add_component<CameraComponent>(camera, CameraComponent{});

    const auto matches = [&](EntityID id, const std::string& filter) {
        return EditorHierarchyFilter::matches_recursive(em, id, filter, state_store.get());
    };

    // 1. Text search (case-insensitive substring)
    assert(matches(parent, "enemies"));
    assert(matches(parent, "scout")); // Parent matches because child matches
    assert(matches(goblin, "scout"));
    assert(!matches(camera, "scout"));

    // 2. Component type filters with prefix ":" or "t:"
    assert(matches(goblin, ":sprite"));
    assert(matches(parent, ":sprite")); // Parent contains child with sprite
    assert(!matches(camera, ":sprite"));

    assert(matches(boss, "t:rb"));
    assert(matches(boss, ":collider"));
    assert(!matches(goblin, ":collider"));

    assert(matches(camera, ":camera"));
    assert(!matches(parent, ":camera"));

    // 3. State filters (:locked, :hidden)
    assert(!matches(parent, ":locked"));
    state_store->set_locked(goblin, true);
    assert(matches(goblin, ":locked"));
    assert(matches(parent, ":locked")); // Parent matches because child is locked
    assert(!matches(boss, ":locked"));

    state_store->set_visible(boss, false);
    assert(matches(boss, ":hidden"));
    assert(!matches(goblin, ":hidden"));

    std::cout << "test_scene_hierarchy_smart_search_and_component_filters passed." << std::endl;
}

void test_remove_missing_hierarchy_child_reference_round_trips() {
    EntityManager em;
    EditorCommandHistory history;
    const EntityID parent = em.create_entity();
    em.add_component<HierarchyComponent>(parent, HierarchyComponent{});
    em.get_component<HierarchyComponent>(parent).children = {777, 888};

    history.execute_command(std::make_unique<RemoveMissingHierarchyChildReferenceCommand>(&em, parent, 777));
    assert(em.get_component<HierarchyComponent>(parent).children == std::vector<EntityID>({888}));

    history.undo();
    assert(em.get_component<HierarchyComponent>(parent).children == std::vector<EntityID>({777, 888}));

    history.redo();
    assert(em.get_component<HierarchyComponent>(parent).children == std::vector<EntityID>({888}));
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
    test_hierarchy_lock_validation();
    test_delete_blocks_locked_descendants_and_restores_metadata();
    test_delete_parent_recursively_deletes_entire_subtree_and_restores_on_undo();
    test_reorder_children_under_parent();
    test_reorder_root_entities();
    test_reorder_rejects_when_parent_changed_externally();
    test_reorder_locked_entity_rejection();
    test_reparent_with_target_index();
    test_duplicate_subtree_hierarchy_remaps_children_and_preserves_transforms();
    test_duplicate_locked_entity_or_subtree_aborts_without_mutation();
    test_duplicate_subtree_under_rotated_scaled_external_parent_preserves_world_offset_and_relative_transforms();
    test_reparent_multiple_entities_preserves_world_transforms_and_order();
    test_reparent_multiple_entities_with_cycle_rejection();
    test_reparent_multiple_entities_with_locked_entity_rejection();
    test_reparent_multiple_entities_unparent_to_root();
    test_scene_hierarchy_smart_search_and_component_filters();
    test_remove_missing_hierarchy_child_reference_round_trips();

    std::cout << "All HierarchyCommandTests passed successfully!" << std::endl;
    return 0;
}
