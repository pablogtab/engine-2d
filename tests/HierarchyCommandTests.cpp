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

int main() {
    test_reparent_preserves_world_transform();
    test_unparent_preserves_world_transform();
    test_non_uniform_and_negative_scale();
    test_hierarchy_cycle_prevention_no_history();
    test_self_parenting_rejection_no_history();
    test_invalid_and_destroyed_parent_no_history();
    test_missing_transform_rejection();
    test_degenerate_parent_scale_rejection();

    std::cout << "All HierarchyCommandTests passed successfully!" << std::endl;
    return 0;
}
