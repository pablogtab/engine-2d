#include "editor/commands/EditorCommandHistory.hpp"
#include "editor/commands/EditorPropertyTransaction.hpp"
#include "editor/commands/EntityCommands.hpp"
#include "ecs/EntityManager.hpp"
#include "events/EventDispatcher.hpp"
#include "physics/PhysicsSystem.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <optional>

namespace {

using namespace Engine;

bool same_color(const SDL_Color& lhs, const SDL_Color& rhs) {
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

bool same_shape(const ColliderShape& lhs, const ColliderShape& rhs) {
    return lhs.type == rhs.type && lhs.offset == rhs.offset && lhs.size == rhs.size &&
           lhs.radius == rhs.radius && lhs.vertices == rhs.vertices &&
           lhs.is_trigger == rhs.is_trigger && lhs.is_one_way == rhs.is_one_way &&
           lhs.category_bits == rhs.category_bits && lhs.mask_bits == rhs.mask_bits;
}

bool same_collider(const ColliderComponent& lhs, const ColliderComponent& rhs) {
    if (lhs.shapes.size() != rhs.shapes.size()) return false;
    for (std::size_t index = 0; index < lhs.shapes.size(); ++index) {
        if (!same_shape(lhs.shapes[index], rhs.shapes[index])) return false;
    }
    return true;
}

void test_entity_rename_round_trips() {
    EntityManager em;
    EditorCommandHistory history;
    EditorPropertyTransaction transactions;
    const EntityID entity = em.create_entity();
    em.add_component<TagComponent>(entity, TagComponent{"Player"});
    auto& tag = em.get_component<TagComponent>(entity);
    const auto key = EditorPropertyTransaction::make_key(entity, "Entity.Name");

    assert(transactions.begin<TagComponent>(
        key,
        tag,
        [&]() -> std::optional<TagComponent> { return em.get_component<TagComponent>(entity); },
        [&](const TagComponent& value) { em.get_component<TagComponent>(entity) = value; },
        "Rename Entity",
        [](const TagComponent& lhs, const TagComponent& rhs) { return lhs.tag == rhs.tag; }
    ));
    tag.tag = "Player_Knight";
    assert(transactions.commit(key, history));
    assert(history.get_undo_count() == 1);

    history.undo();
    assert(em.get_component<TagComponent>(entity).tag == "Player");
    history.redo();
    assert(em.get_component<TagComponent>(entity).tag == "Player_Knight");
}

void test_sprite_compound_edit_round_trips() {
    EntityManager em;
    EditorCommandHistory history;
    EditorPropertyTransaction transactions;
    const EntityID entity = em.create_entity();
    em.add_component<SpriteComponent>(entity, SpriteComponent{});
    auto& sprite = em.get_component<SpriteComponent>(entity);
    const SpriteComponent before = sprite;
    const auto key = EditorPropertyTransaction::make_key(entity, "Sprite.Appearance");

    assert(transactions.begin<SpriteComponent>(
        key,
        before,
        [&]() -> std::optional<SpriteComponent> { return em.get_component<SpriteComponent>(entity); },
        [&](const SpriteComponent& value) { em.get_component<SpriteComponent>(entity) = value; },
        "Edit Sprite Appearance",
        [](const SpriteComponent& lhs, const SpriteComponent& rhs) {
            return lhs.size == rhs.size && same_color(lhs.color, rhs.color) &&
                   lhs.visible == rhs.visible && lhs.flip_x == rhs.flip_x &&
                   lhs.flip_y == rhs.flip_y;
        }
    ));
    sprite.size = {128.0f, 32.0f};
    sprite.color = {32, 64, 96, 128};
    sprite.flip_x = true;
    assert(transactions.commit(key, history));

    history.undo();
    const auto& undone = em.get_component<SpriteComponent>(entity);
    assert(undone.size == before.size);
    assert(same_color(undone.color, before.color));
    assert(!undone.flip_x);

    history.redo();
    const auto& redone = em.get_component<SpriteComponent>(entity);
    assert(redone.size == glm::vec2(128.0f, 32.0f));
    assert(same_color(redone.color, SDL_Color{32, 64, 96, 128}));
    assert(redone.flip_x);
}

void test_component_presence_round_trips_exact_state() {
    EntityManager em;
    EditorCommandHistory history;
    const EntityID entity = em.create_entity();
    int side_effect_count = 0;
    auto side_effect = [&](EntityManager&, EntityID affected) {
        assert(affected == entity);
        ++side_effect_count;
    };

    RigidBodyComponent rigidbody = RigidBodyComponent::Dynamic(1.5f, 0.6f, 0.25f, false);
    rigidbody.mass = 3.5f;
    history.execute_command(std::make_unique<SetComponentCommand<RigidBodyComponent>>(
        &em, entity, std::nullopt, rigidbody, "Add RigidBody 2D", side_effect
    ));
    assert(em.has_component<RigidBodyComponent>(entity));
    assert(em.get_component<RigidBodyComponent>(entity).mass == 3.5f);
    assert(side_effect_count == 1);

    history.undo();
    assert(!em.has_component<RigidBodyComponent>(entity));
    history.redo();
    assert(em.has_component<RigidBodyComponent>(entity));
    assert(em.get_component<RigidBodyComponent>(entity).restitution == 0.6f);
    assert(!em.get_component<RigidBodyComponent>(entity).fixed_rotation);
    assert(side_effect_count == 3);

    ColliderComponent collider;
    collider.add_box({40.0f, 20.0f}, {2.0f, 3.0f}, true, CollisionCategory::PLAYER, CollisionCategory::GROUND);
    collider.add_polygon({{-5.0f, -2.0f}, {6.0f, -2.0f}, {1.0f, 8.0f}}, {4.0f, 1.0f});
    em.add_component<ColliderComponent>(entity, collider);
    history.execute_command(std::make_unique<SetComponentCommand<ColliderComponent>>(
        &em, entity, collider, std::nullopt, "Remove Collider 2D", side_effect
    ));
    assert(!em.has_component<ColliderComponent>(entity));
    history.undo();
    assert(em.has_component<ColliderComponent>(entity));
    assert(same_collider(em.get_component<ColliderComponent>(entity), collider));
    history.redo();
    assert(!em.has_component<ColliderComponent>(entity));
}

void test_physics_rebuild_updates_fixture_without_preview_churn() {
    PhysicsSystem::clear();
    EntityManager em;
    EventDispatcher events;
    const EntityID entity = em.create_entity();
    em.add_component<TransformComponent>(entity, TransformComponent{0.0f, 0.0f});
    em.add_component<RigidBodyComponent>(entity, RigidBodyComponent::Static());
    em.add_component<ColliderComponent>(entity, ColliderComponent::Box({20.0f, 20.0f}));

    PhysicsSystem::update(em, events, 0.0f);
    assert(PhysicsSystem::raycast({-30.0f, 0.0f}, {30.0f, 0.0f}).entity_id == entity);
    assert(!PhysicsSystem::raycast({70.0f, 0.0f}, {130.0f, 0.0f}).hit);

    auto& collider = em.get_component<ColliderComponent>(entity);
    collider.shapes.front().offset.x = 100.0f;
    PhysicsSystem::rebuild_body(em, entity);

    assert(!PhysicsSystem::raycast({-30.0f, 0.0f}, {30.0f, 0.0f}).hit);
    assert(PhysicsSystem::raycast({70.0f, 0.0f}, {130.0f, 0.0f}).entity_id == entity);
    PhysicsSystem::clear();
}

} // namespace

int main() {
    test_entity_rename_round_trips();
    test_sprite_compound_edit_round_trips();
    test_component_presence_round_trips_exact_state();
    test_physics_rebuild_updates_fixture_without_preview_churn();
    std::cout << "InspectorCommandTests passed\n";
    return 0;
}
