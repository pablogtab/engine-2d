#include "editor/commands/EditorCommandHistory.hpp"
#include "editor/commands/EditorPropertyTransaction.hpp"
#include "editor/commands/EntityCommands.hpp"
#include "editor/inspector/InspectorPropertyPolicies.hpp"
#include "editor/inspector/MultiComponentInspectorTransaction.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/TransformSystem.hpp"
#include "events/EventDispatcher.hpp"
#include "physics/PhysicsSystem.hpp"
#include "tilemap/Tilemap.hpp"
#include "tilemap/TilemapColliderSystem.hpp"
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

std::size_t generated_collider_count(EntityManager& em, EntityID owner) {
    std::size_t count = 0;
    em.for_each<TilemapGeneratedColliderComponent>(
        [&](EntityID, const TilemapGeneratedColliderComponent& generated) {
            if (generated.owner == owner) ++count;
        }
    );
    return count;
}

bool same_authored_tilemap(const TilemapComponent& lhs, const TilemapComponent& rhs) {
    return lhs.tile_size == rhs.tile_size &&
        lhs.tile_pixel_size == rhs.tile_pixel_size &&
        lhs.tileset_path == rhs.tileset_path &&
        lhs.tiles == rhs.tiles &&
        lhs.solid_tile_ids == rhs.solid_tile_ids &&
        lhs.generate_colliders == rhs.generate_colliders &&
        lhs.is_trigger == rhs.is_trigger &&
        lhs.is_one_way == rhs.is_one_way &&
        lhs.category_bits == rhs.category_bits &&
        lhs.mask_bits == rhs.mask_bits &&
        lhs.friction == rhs.friction &&
        lhs.restitution == rhs.restitution;
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

void test_tilemap_edit_rebuilds_only_on_commit_and_round_trips() {
    EntityManager em;
    EditorCommandHistory history;
    EditorPropertyTransaction transactions;
    const EntityID entity = em.create_entity();
    em.add_component<TransformComponent>(entity, TransformComponent{0.0f, 0.0f});
    TilemapComponent initial;
    initial.set_tile(0, 0, 1);
    initial.set_tile(1, 0, 1);
    em.add_component<TilemapComponent>(entity, initial);
    TilemapColliderSystem::build_composite_colliders(em, entity);
    assert(generated_collider_count(em, entity) == 1);

    const auto key = EditorPropertyTransaction::make_key(entity, "Tilemap.TileSize");
    const auto apply = [&](const TilemapComponent& value) {
        em.get_component<TilemapComponent>(entity) = value;
        TilemapColliderSystem::build_composite_colliders(em, entity);
    };
    assert(transactions.begin<TilemapComponent>(
        key,
        initial,
        [&]() -> std::optional<TilemapComponent> { return em.get_component<TilemapComponent>(entity); },
        apply,
        "Edit Tilemap Tile Size",
        same_authored_tilemap,
        [&]() { TilemapColliderSystem::build_composite_colliders(em, entity); }
    ));

    auto& live = em.get_component<TilemapComponent>(entity);
    live.tile_size = 48.0f;
    live.friction = 0.25f;
    const EntityID preview_collider = em.get_component<HierarchyComponent>(entity).children.front();
    assert(em.get_component<ColliderComponent>(preview_collider).shapes.front().size.x == 64.0f);

    assert(transactions.commit(key, history));
    assert(history.get_undo_count() == 1);
    assert(generated_collider_count(em, entity) == 1);
    EntityID committed_collider = em.get_component<HierarchyComponent>(entity).children.front();
    assert(em.get_component<ColliderComponent>(committed_collider).shapes.front().size.x == 96.0f);

    history.undo();
    assert(same_authored_tilemap(em.get_component<TilemapComponent>(entity), initial));
    assert(generated_collider_count(em, entity) == 1);
    EntityID undone_collider = em.get_component<HierarchyComponent>(entity).children.front();
    assert(em.get_component<ColliderComponent>(undone_collider).shapes.front().size.x == 64.0f);

    history.redo();
    assert(em.get_component<TilemapComponent>(entity).tile_size == 48.0f);
    assert(em.get_component<TilemapComponent>(entity).friction == 0.25f);
    assert(generated_collider_count(em, entity) == 1);
}

void test_clear_and_remove_tilemap_restore_authored_and_generated_state() {
    EntityManager em;
    EditorCommandHistory history;
    EditorPropertyTransaction transactions;
    const EntityID entity = em.create_entity();
    em.add_component<TransformComponent>(entity, TransformComponent{0.0f, 0.0f});
    TilemapComponent initial;
    initial.set_tile(-1, 2, 1);
    initial.set_tile(0, 2, 1);
    em.add_component<TilemapComponent>(entity, initial);
    TilemapColliderSystem::build_composite_colliders(em, entity);

    const auto clear_key = EditorPropertyTransaction::make_key(entity, "Tilemap.Clear");
    assert(transactions.begin<TilemapComponent>(
        clear_key,
        initial,
        [&]() -> std::optional<TilemapComponent> { return em.get_component<TilemapComponent>(entity); },
        [&](const TilemapComponent& value) {
            em.get_component<TilemapComponent>(entity) = value;
            TilemapColliderSystem::build_composite_colliders(em, entity);
        },
        "Clear Tilemap",
        same_authored_tilemap,
        [&]() { TilemapColliderSystem::build_composite_colliders(em, entity); }
    ));
    em.get_component<TilemapComponent>(entity).clear();
    assert(transactions.commit(clear_key, history));
    assert(em.get_component<TilemapComponent>(entity).empty());
    assert(generated_collider_count(em, entity) == 0);

    history.undo();
    assert(em.get_component<TilemapComponent>(entity).tile_count() == 2);
    assert(generated_collider_count(em, entity) == 1);
    history.redo();
    assert(em.get_component<TilemapComponent>(entity).empty());
    assert(generated_collider_count(em, entity) == 0);

    history.undo();
    auto rebuild = [](EntityManager& command_em, EntityID id) {
        if (command_em.has_component<TilemapComponent>(id)) {
            TilemapColliderSystem::build_composite_colliders(command_em, id);
        } else {
            TilemapColliderSystem::clear_composite_colliders(command_em, id);
        }
    };
    const TilemapComponent before_remove = em.get_component<TilemapComponent>(entity);
    history.execute_command(std::make_unique<SetComponentCommand<TilemapComponent>>(
        &em,
        entity,
        before_remove,
        std::nullopt,
        "Remove Tilemap Grid",
        rebuild
    ));
    assert(!em.has_component<TilemapComponent>(entity));
    assert(generated_collider_count(em, entity) == 0);

    history.undo();
    assert(em.has_component<TilemapComponent>(entity));
    assert(same_authored_tilemap(em.get_component<TilemapComponent>(entity), before_remove));
    assert(generated_collider_count(em, entity) == 1);
}

void test_delete_tilemap_cleans_and_restores_generated_colliders() {
    EntityManager em;
    EditorCommandHistory history;
    const EntityID entity = em.create_entity();
    em.add_component<TagComponent>(entity, TagComponent{"Tilemap"});
    em.add_component<TransformComponent>(entity, TransformComponent{0.0f, 0.0f});
    TilemapComponent tilemap;
    tilemap.set_tile(0, 0, 1);
    em.add_component<TilemapComponent>(entity, tilemap);
    TilemapColliderSystem::build_composite_colliders(em, entity);
    assert(generated_collider_count(em, entity) == 1);

    history.execute_command(std::make_unique<DeleteEntitiesCommand>(
        &em,
        nullptr,
        std::vector<EntityID>{entity}
    ));
    assert(!em.is_alive(entity));
    assert(generated_collider_count(em, entity) == 0);

    history.undo();
    assert(em.is_alive(entity));
    assert(em.has_component<TilemapComponent>(entity));
    assert(em.get_component<TilemapComponent>(entity).tile_count() == 1);
    assert(generated_collider_count(em, entity) == 1);

    history.redo();
    assert(!em.is_alive(entity));
    assert(generated_collider_count(em, entity) == 0);
}

void test_animation_authored_edits_round_trip_without_runtime_state() {
    EntityManager em;
    EditorCommandHistory history;
    EditorPropertyTransaction transactions;
    const EntityID entity = em.create_entity();
    AnimationComponent initial;
    initial.columns = 4;
    initial.rows = 2;
    initial.add_clip("Idle", 0, 2, 8.0f, true);
    em.add_component<AnimationComponent>(entity, initial);
    em.add_component<AnimationPlaybackStateComponent>(
        entity,
        AnimationPlaybackStateComponent{0, 1, 0.5f, true, false, true}
    );

    const auto reset_runtime = [&](EntityManager& command_em, EntityID id) {
        if (command_em.has_component<AnimationPlaybackStateComponent>(id)) {
            command_em.remove_component<AnimationPlaybackStateComponent>(id);
        }
    };
    const auto key = EditorPropertyTransaction::make_key(entity, "Animation.Clips");
    assert(transactions.begin<AnimationComponent>(
        key,
        initial,
        [&]() -> std::optional<AnimationComponent> { return em.get_component<AnimationComponent>(entity); },
        [&](const AnimationComponent& value) {
            em.get_component<AnimationComponent>(entity) = value;
            reset_runtime(em, entity);
        },
        "Edit Animation Clips",
        std::equal_to<AnimationComponent>{},
        [&]() { reset_runtime(em, entity); }
    ));

    auto& animation = em.get_component<AnimationComponent>(entity);
    animation.columns = 8;
    animation.spacing_x = 2.0f;
    animation.add_clip("Run", 2, 4, 12.0f, true);
    animation.default_clip_index = 1;
    assert(em.has_component<AnimationPlaybackStateComponent>(entity));

    assert(transactions.commit(key, history));
    assert(!em.has_component<AnimationPlaybackStateComponent>(entity));
    assert(history.get_undo_count() == 1);

    history.undo();
    assert(em.get_component<AnimationComponent>(entity) == initial);
    assert(!em.has_component<AnimationPlaybackStateComponent>(entity));

    history.redo();
    const auto& redone = em.get_component<AnimationComponent>(entity);
    assert(redone.columns == 8);
    assert(redone.clips.size() == 2);
    assert(redone.clips[1].name == "Run");
    assert(redone.default_clip_index == 1);
    assert(!em.has_component<AnimationPlaybackStateComponent>(entity));
}

void test_camera_edits_and_primary_selection_round_trip() {
    EntityManager em;
    EditorCommandHistory history;
    EditorPropertyTransaction transactions;
    const EntityID first = em.create_entity();
    const EntityID second = em.create_entity();
    CameraComponent first_camera;
    first_camera.is_primary = true;
    CameraComponent second_camera;
    em.add_component<CameraComponent>(first, first_camera);
    em.add_component<CameraComponent>(second, second_camera);

    const auto key = EditorPropertyTransaction::make_key(second, "Camera.Settings");
    assert(transactions.begin<CameraComponent>(
        key,
        second_camera,
        [&]() -> std::optional<CameraComponent> { return em.get_component<CameraComponent>(second); },
        [&](const CameraComponent& value) { em.get_component<CameraComponent>(second) = value; },
        "Edit Camera Settings"
    ));
    auto& edited = em.get_component<CameraComponent>(second);
    edited.zoom = 2.5f;
    edited.projection = CameraProjectionMode::PixelPerfect;
    edited.follow_target = first;
    edited.follow_offset = {12.0f, -6.0f};
    edited.follow_speed = 9.0f;
    edited.enable_bounds = true;
    edited.bounds = {-100.0f, -50.0f, 100.0f, 50.0f};
    const CameraComponent expected = edited;
    assert(transactions.commit(key, history));

    history.undo();
    assert(em.get_component<CameraComponent>(second) == second_camera);
    history.redo();
    assert(em.get_component<CameraComponent>(second) == expected);

    history.execute_command(std::make_unique<SetPrimaryCameraCommand>(&em, second));
    assert(!em.get_component<CameraComponent>(first).is_primary);
    assert(em.get_component<CameraComponent>(second).is_primary);
    history.undo();
    assert(em.get_component<CameraComponent>(first).is_primary);
    assert(!em.get_component<CameraComponent>(second).is_primary);
    history.redo();
    assert(!em.get_component<CameraComponent>(first).is_primary);
    assert(em.get_component<CameraComponent>(second).is_primary);
}

void test_camera_component_removal_restores_exact_state() {
    EntityManager em;
    EditorCommandHistory history;
    const EntityID entity = em.create_entity();
    CameraComponent camera;
    camera.is_primary = true;
    camera.zoom = 3.0f;
    camera.projection = CameraProjectionMode::Expand;
    camera.follow_speed = 4.5f;
    em.add_component<CameraComponent>(entity, camera);

    history.execute_command(std::make_unique<SetComponentCommand<CameraComponent>>(
        &em, entity, camera, std::nullopt, "Remove Camera 2D"
    ));
    assert(!em.has_component<CameraComponent>(entity));
    history.undo();
    assert(em.get_component<CameraComponent>(entity) == camera);
    history.redo();
    assert(!em.has_component<CameraComponent>(entity));
}

void test_transform_command_preserves_child_local_coordinates() {
    EntityManager em;
    const EntityID parent = em.create_entity();
    const EntityID child = em.create_entity();
    em.add_component<TransformComponent>(parent, TransformComponent{100.0f, 50.0f});
    em.add_component<TransformComponent>(child, TransformComponent{10.0f, 5.0f});
    em.set_parent(child, parent);
    TransformSystem::update(em);

    const TransformComponent before = em.get_component<TransformComponent>(child);
    TransformComponent after = before;
    after.position = {20.0f, 15.0f, 0.0f};
    TransformEntitiesCommand command(
        &em,
        {{child, before}},
        {{child, after}},
        "Move Child"
    );

    command.execute();
    const auto& moved = em.get_component<TransformComponent>(child);
    assert(moved.position == glm::vec3(20.0f, 15.0f, 0.0f));
    assert(moved.world_position == glm::vec3(120.0f, 65.0f, 0.0f));

    command.undo();
    const auto& restored = em.get_component<TransformComponent>(child);
    assert(restored.position == glm::vec3(10.0f, 5.0f, 0.0f));
    assert(restored.world_position == glm::vec3(110.0f, 55.0f, 0.0f));
}

void test_transform_scale_command_rebuilds_physics_fixture() {
    PhysicsSystem::clear();
    EntityManager em;
    EventDispatcher events;
    const EntityID entity = em.create_entity();
    em.add_component<TransformComponent>(entity, TransformComponent{0.0f, 0.0f});
    em.add_component<RigidBodyComponent>(entity, RigidBodyComponent::Static());
    em.add_component<ColliderComponent>(entity, ColliderComponent::Box({20.0f, 20.0f}));
    TransformSystem::update(em);
    PhysicsSystem::update(em, events, 0.0f);
    assert(!PhysicsSystem::raycast({15.0f, -15.0f}, {15.0f, 15.0f}).hit);

    const TransformComponent before = em.get_component<TransformComponent>(entity);
    TransformComponent after = before;
    after.scale = {2.0f, 1.0f};
    TransformEntitiesCommand command(&em, {{entity, before}}, {{entity, after}}, "Scale Entity");
    command.execute();
    assert(PhysicsSystem::raycast({15.0f, -15.0f}, {15.0f, 15.0f}).entity_id == entity);

    command.undo();
    assert(!PhysicsSystem::raycast({15.0f, -15.0f}, {15.0f, 15.0f}).hit);
    PhysicsSystem::clear();
}

void test_scaled_collider_offset_matches_fixture_geometry() {
    PhysicsSystem::clear();
    EntityManager em;
    EventDispatcher events;
    const EntityID entity = em.create_entity();

    TransformComponent transform;
    transform.scale = {2.0f, 1.0f};
    em.add_component<TransformComponent>(entity, transform);
    em.add_component<RigidBodyComponent>(entity, RigidBodyComponent::Static());
    em.add_component<ColliderComponent>(
        entity,
        ColliderComponent::Box({10.0f, 10.0f}, {10.0f, 0.0f})
    );

    TransformSystem::update(em);
    PhysicsSystem::update(em, events, 0.0f);

    // Scale 2x moves the local offset from 10 to 20 and expands the half-width
    // from 5 to 10, so the physical fixture covers world X [10, 30].
    assert(PhysicsSystem::raycast({27.0f, -20.0f}, {27.0f, 20.0f}).entity_id == entity);
    PhysicsSystem::clear();
}

void test_scaled_player_fixture_reaches_trigger() {
    PhysicsSystem::clear();
    EntityManager em;
    EventDispatcher events;
    bool trigger_started = false;
    events.subscribe<CollisionEvent>([&](const CollisionEvent& event) {
        trigger_started = event.is_trigger &&
            ((event.entity_a == 1 && event.entity_b == 2) ||
             (event.entity_a == 2 && event.entity_b == 1));
    });

    const EntityID player = em.create_entity();
    TransformComponent player_transform;
    player_transform.scale = {2.0f, 2.0f};
    em.add_component<TransformComponent>(player, player_transform);
    auto player_body = RigidBodyComponent::Dynamic();
    player_body.gravity_scale = 0.0f;
    em.add_component<RigidBodyComponent>(player, player_body);
    em.add_component<ColliderComponent>(player, ColliderComponent::Box({44.0f, 58.0f}));

    const EntityID coin = em.create_entity();
    em.add_component<TransformComponent>(coin, TransformComponent{glm::vec3(52.0f, 0.0f, 0.0f)});
    em.add_component<RigidBodyComponent>(coin, RigidBodyComponent::Static());
    em.add_component<ColliderComponent>(coin, ColliderComponent::Circle(14.0f, {}, true));

    TransformSystem::update(em);
    PhysicsSystem::update(em, events, 1.0f / 60.0f);
    assert(trigger_started);
    PhysicsSystem::clear();
}

void test_central_property_policies_sanitize_values() {
    namespace Policies = InspectorPropertyPolicies;
    assert(Policies::Camera::Zoom.sanitize(0.01f) == 0.1f);
    assert(Policies::Camera::Zoom.sanitize(30.0f) == 20.0f);
    assert(Policies::Transform::Scale.sanitize(0.0f) == 0.01f);
    assert(Policies::Sprite::SortingOrder.sanitize(50000) == 32767);
    assert(Policies::Transform::Position.sanitize(-1000000.0f) == -1000000.0f);
    assert(Policies::bounded_int(1.0f, 0, 0).sanitize(9) == 0);
}

void test_multi_component_edit_is_atomic_and_preserves_unedited_axes() {
    EntityManager em;
    EditorCommandHistory history;
    EditorPropertyTransaction transactions;

    const EntityID first = em.create_entity();
    const EntityID second = em.create_entity();
    TransformComponent first_transform;
    first_transform.position = {10.0f, 20.0f, 1.0f};
    TransformComponent second_transform;
    second_transform.position = {-5.0f, 80.0f, 2.0f};
    em.add_component<TransformComponent>(first, first_transform);
    em.add_component<TransformComponent>(second, second_transform);

    const std::vector<EntityID> selection = sorted_selection({second, first});
    const auto before = capture_selected_components<TransformComponent>(em, selection);
    assert(before.size() == 2);
    assert((selected_values_mixed<TransformComponent, float>(
        before, [](const TransformComponent& transform) { return transform.position.x; }
    )));
    assert((selected_values_mixed<TransformComponent, float>(
        before, [](const TransformComponent& transform) { return transform.position.y; }
    )));

    const auto key = EditorPropertyTransaction::make_key(
        selection_transaction_owner(selection), "MultiSelection.Transform.Position"
    );
    const auto same_transform_states = [](const auto& lhs, const auto& rhs) {
        return selected_component_states_equal(
            lhs, rhs, [](const TransformComponent& a, const TransformComponent& b) {
                return a.position == b.position && a.scale == b.scale &&
                    a.rotation == b.rotation && a.pivot == b.pivot &&
                    a.skew == b.skew && a.visible == b.visible;
            }
        );
    };
    assert(transactions.begin<SelectedComponentStates<TransformComponent>>(
        key,
        before,
        [&]() -> std::optional<SelectedComponentStates<TransformComponent>> {
            return capture_selected_components<TransformComponent>(em, selection);
        },
        [&](const SelectedComponentStates<TransformComponent>& states) {
            for (const auto& [entity, transform] : states) {
                em.get_component<TransformComponent>(entity) = transform;
            }
        },
        "Edit Selected Transform Position",
        same_transform_states
    ));

    em.get_component<TransformComponent>(first).position.x = 42.0f;
    em.get_component<TransformComponent>(second).position.x = 42.0f;
    assert(transactions.commit(key, history));
    assert(history.get_undo_count() == 1);
    assert(em.get_component<TransformComponent>(first).position.y == 20.0f);
    assert(em.get_component<TransformComponent>(second).position.y == 80.0f);

    history.undo();
    assert(em.get_component<TransformComponent>(first).position == first_transform.position);
    assert(em.get_component<TransformComponent>(second).position == second_transform.position);
    history.redo();
    assert(em.get_component<TransformComponent>(first).position == glm::vec3(42.0f, 20.0f, 1.0f));
    assert(em.get_component<TransformComponent>(second).position == glm::vec3(42.0f, 80.0f, 2.0f));

    const auto edited = capture_selected_components<TransformComponent>(em, selection);
    assert(transactions.begin<SelectedComponentStates<TransformComponent>>(
        key,
        edited,
        [&]() -> std::optional<SelectedComponentStates<TransformComponent>> {
            return capture_selected_components<TransformComponent>(em, selection);
        },
        [&](const SelectedComponentStates<TransformComponent>& states) {
            for (const auto& [entity, transform] : states) {
                em.get_component<TransformComponent>(entity) = transform;
            }
        },
        "Edit Selected Transform Position",
        same_transform_states
    ));
    em.get_component<TransformComponent>(first).position.x = 100.0f;
    em.get_component<TransformComponent>(second).position.x = 100.0f;
    assert(transactions.cancel(key));
    assert(em.get_component<TransformComponent>(first).position == glm::vec3(42.0f, 20.0f, 1.0f));
    assert(em.get_component<TransformComponent>(second).position == glm::vec3(42.0f, 80.0f, 2.0f));
    assert(history.get_undo_count() == 1);
}

} // namespace

int main() {
    test_entity_rename_round_trips();
    test_sprite_compound_edit_round_trips();
    test_component_presence_round_trips_exact_state();
    test_physics_rebuild_updates_fixture_without_preview_churn();
    test_tilemap_edit_rebuilds_only_on_commit_and_round_trips();
    test_clear_and_remove_tilemap_restore_authored_and_generated_state();
    test_delete_tilemap_cleans_and_restores_generated_colliders();
    test_animation_authored_edits_round_trip_without_runtime_state();
    test_camera_edits_and_primary_selection_round_trip();
    test_camera_component_removal_restores_exact_state();
    test_transform_command_preserves_child_local_coordinates();
    test_transform_scale_command_rebuilds_physics_fixture();
    test_scaled_collider_offset_matches_fixture_geometry();
    test_scaled_player_fixture_reaches_trigger();
    test_central_property_policies_sanitize_values();
    test_multi_component_edit_is_atomic_and_preserves_unedited_axes();
    std::cout << "InspectorCommandTests passed\n";
    return 0;
}
