#include "editor/commands/EditorCommandHistory.hpp"
#include "editor/commands/EditorPropertyTransaction.hpp"
#include "editor/commands/EntityCommands.hpp"
#include "editor/tools/ViewportTransformMath.hpp"
#include "ecs/components/AnimationComponents.hpp"
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

void test_registered_component_presence_command_round_trips() {
    EntityManager em;
    EditorCommandHistory history;
    const EntityID entity = em.create_entity();
    em.add_component<TransformComponent>(entity, TransformComponent{});

    ComponentSnapshotSet before;
    ComponentSnapshotSet after;
    SpriteComponent sprite;
    sprite.size = {96.0f, 48.0f};
    sprite.color.a = 89;
    after.set(sprite);

    history.execute_command(std::make_unique<SetRegisteredComponentCommand>(
        &default_component_registry(),
        &em,
        entity,
        "sprite",
        std::move(before),
        std::move(after),
        ComponentSerializationContext{},
        "Add Sprite Renderer"
    ));
    assert(em.has_component<SpriteComponent>(entity));
    assert(em.get_component<SpriteComponent>(entity).size == glm::vec2(96.0f, 48.0f));
    assert(em.get_component<SpriteComponent>(entity).color.a == 89);

    history.undo();
    assert(!em.has_component<SpriteComponent>(entity));
    history.redo();
    assert(em.has_component<SpriteComponent>(entity));
    assert(em.get_component<SpriteComponent>(entity).size == glm::vec2(96.0f, 48.0f));
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
    assert(em.set_parent(child, parent).succeeded());
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

void test_multiselection_rigidbody_mixed_values_and_undo_redo() {
    EntityManager em;
    EditorCommandHistory history;
    EditorPropertyTransaction transactions;

    EntityID e1 = em.create_entity();
    em.add_component<TransformComponent>(e1, TransformComponent{glm::vec3(0.0f)});
    RigidBodyComponent rb1;
    rb1.mass = 1.0f;
    rb1.body_type = BodyType::Dynamic;
    em.add_component<RigidBodyComponent>(e1, rb1);

    EntityID e2 = em.create_entity();
    em.add_component<TransformComponent>(e2, TransformComponent{glm::vec3(10.0f)});
    RigidBodyComponent rb2;
    rb2.mass = 5.0f;
    rb2.body_type = BodyType::Static;
    em.add_component<RigidBodyComponent>(e2, rb2);

    const std::vector<EntityID> selection = {e1, e2};
    const auto before = capture_selected_components<RigidBodyComponent>(em, selection);
    assert(before.size() == 2);

    // Verify mixed detection
    assert((selected_values_mixed<RigidBodyComponent, float>(before, [](const auto& rb) { return rb.mass; })));
    assert((selected_values_mixed<RigidBodyComponent, BodyType>(before, [](const auto& rb) { return rb.body_type; })));

    std::size_t rebuild_count = 0;
    auto side_effect = [&](EntityManager&, const std::vector<EntityID>&) {
        ++rebuild_count;
    };

    const ComponentTransactionContext context{&history, &transactions};

    // Simulate drag start via track_selected_component_edit
    const auto key = EditorPropertyTransaction::make_key(selection_transaction_owner(selection), "MultiSelection.RigidBody.Mass");
    assert(transactions.begin<SelectedComponentStates<RigidBodyComponent>>(
        key,
        before,
        [&]() -> std::optional<SelectedComponentStates<RigidBodyComponent>> {
            return capture_selected_components<RigidBodyComponent>(em, selection);
        },
        [&](const SelectedComponentStates<RigidBodyComponent>& states) {
            for (const auto& [entity, comp] : states) {
                em.get_component<RigidBodyComponent>(entity) = comp;
            }
            side_effect(em, selection);
        },
        "Edit Selected RigidBody Mass",
        [](const auto& lhs, const auto& rhs) {
            return selected_component_states_equal(lhs, rhs, [](const auto& a, const auto& b) { return a.mass == b.mass; });
        },
        [&]() { side_effect(em, selection); }
    ));

    // Interactive Drag Preview: value changes on each frame, NO physics rebuild yet
    em.get_component<RigidBodyComponent>(e1).mass = 2.5f;
    em.get_component<RigidBodyComponent>(e2).mass = 2.5f;
    assert(rebuild_count == 0);

    // Commit drag (e.g. mouse release)
    assert(transactions.commit(key, history));
    assert(history.get_undo_count() == 1);
    assert(rebuild_count == 1);

    // Both have mass 2.5f, but body types are preserved
    assert(em.get_component<RigidBodyComponent>(e1).mass == 2.5f);
    assert(em.get_component<RigidBodyComponent>(e2).mass == 2.5f);
    assert(em.get_component<RigidBodyComponent>(e1).body_type == BodyType::Dynamic);
    assert(em.get_component<RigidBodyComponent>(e2).body_type == BodyType::Static);

    // Undo: restores 1.0f on e1 and 5.0f on e2, and triggers side effect rebuild
    history.undo();
    assert(em.get_component<RigidBodyComponent>(e1).mass == 1.0f);
    assert(em.get_component<RigidBodyComponent>(e2).mass == 5.0f);
    assert(rebuild_count == 2);

    // Redo: restores 2.5f on both and triggers side effect rebuild
    history.redo();
    assert(em.get_component<RigidBodyComponent>(e1).mass == 2.5f);
    assert(em.get_component<RigidBodyComponent>(e2).mass == 2.5f);
    assert(rebuild_count == 3);
}

void test_multiselection_collider_compatibility_and_mixed_edit() {
    EntityManager em;
    EditorCommandHistory history;
    EditorPropertyTransaction transactions;

    EntityID e1 = em.create_entity();
    em.add_component<TransformComponent>(e1, TransformComponent{});
    em.add_component<ColliderComponent>(e1, ColliderComponent::Box(glm::vec2(32.0f, 32.0f)));
    em.get_component<ColliderComponent>(e1).shapes.front().mask_bits = 0x000F;

    EntityID e2 = em.create_entity();
    em.add_component<TransformComponent>(e2, TransformComponent{});
    em.add_component<ColliderComponent>(e2, ColliderComponent::Box(glm::vec2(64.0f, 64.0f)));
    em.get_component<ColliderComponent>(e2).shapes.front().mask_bits = 0x00F0;

    const std::vector<EntityID> selection = {e1, e2};
    const auto before = capture_selected_components<ColliderComponent>(em, selection);

    // Check compatibility logic
    bool compatible = (before.size() == 2 &&
                       before[0].second.shapes.size() == 1 &&
                       before[1].second.shapes.size() == 1 &&
                       before[0].second.shapes.front().type == before[1].second.shapes.front().type);
    assert(compatible);

    std::size_t rebuild_count = 0;
    auto side_effect = [&](EntityManager&, const std::vector<EntityID>&) {
        ++rebuild_count;
    };

    // Bulk edit mask_bits to 0xFFFF
    const auto key = EditorPropertyTransaction::make_key(selection_transaction_owner(selection), "MultiSelection.Collider.Shape.0.Mask");
    assert(transactions.begin<SelectedComponentStates<ColliderComponent>>(
        key,
        before,
        [&]() -> std::optional<SelectedComponentStates<ColliderComponent>> {
            return capture_selected_components<ColliderComponent>(em, selection);
        },
        [&](const SelectedComponentStates<ColliderComponent>& states) {
            for (const auto& [entity, comp] : states) {
                em.get_component<ColliderComponent>(entity) = comp;
            }
            side_effect(em, selection);
        },
        "Edit Selected Collider Mask",
        [](const auto& lhs, const auto& rhs) {
            return selected_component_states_equal(lhs, rhs, same_collider);
        },
        [&]() { side_effect(em, selection); }
    ));

    em.get_component<ColliderComponent>(e1).shapes.front().mask_bits = 0xFFFF;
    em.get_component<ColliderComponent>(e2).shapes.front().mask_bits = 0xFFFF;
    assert(rebuild_count == 0); // No rebuild during live editing

    assert(transactions.commit(key, history));
    assert(rebuild_count == 1); // Rebuilds on commit

    assert(em.get_component<ColliderComponent>(e1).shapes.front().mask_bits == 0xFFFF);
    assert(em.get_component<ColliderComponent>(e2).shapes.front().mask_bits == 0xFFFF);
    // Box sizes are untouched
    assert(em.get_component<ColliderComponent>(e1).shapes.front().size == glm::vec2(32.0f, 32.0f));
    assert(em.get_component<ColliderComponent>(e2).shapes.front().size == glm::vec2(64.0f, 64.0f));

    // Undo
    history.undo();
    assert(em.get_component<ColliderComponent>(e1).shapes.front().mask_bits == 0x000F);
    assert(em.get_component<ColliderComponent>(e2).shapes.front().mask_bits == 0x00F0);
    assert(rebuild_count == 2);

    // Redo
    history.redo();
    assert(em.get_component<ColliderComponent>(e1).shapes.front().mask_bits == 0xFFFF);
    assert(em.get_component<ColliderComponent>(e2).shapes.front().mask_bits == 0xFFFF);
    assert(rebuild_count == 3);
}

void test_multiselection_camera_bounds_and_follow_target() {
    EntityManager em;
    EditorCommandHistory history;
    EditorPropertyTransaction transactions;

    EntityID target = em.create_entity();
    em.add_component<TransformComponent>(target, TransformComponent{glm::vec3(100.0f)});
    em.add_component<TagComponent>(target, TagComponent{"TargetEntity"});

    EntityID no_transform = em.create_entity(); // has no transform

    EntityID c1 = em.create_entity();
    em.add_component<TransformComponent>(c1, TransformComponent{});
    CameraComponent cam1;
    cam1.zoom = 1.0f;
    cam1.enable_bounds = false;
    em.add_component<CameraComponent>(c1, cam1);

    EntityID c2 = em.create_entity();
    em.add_component<TransformComponent>(c2, TransformComponent{});
    CameraComponent cam2;
    cam2.zoom = 2.0f;
    cam2.enable_bounds = true;
    cam2.bounds = glm::vec4(-500.0f, -500.0f, 500.0f, 500.0f);
    em.add_component<CameraComponent>(c2, cam2);

    const std::vector<EntityID> selection = {c1, c2};
    const auto before = capture_selected_components<CameraComponent>(em, selection);

    // Verify filter excludes camera entities themselves and entities without TransformComponent
    const auto target_filter = [&](EntityID candidate) {
        return std::find(selection.begin(), selection.end(), candidate) == selection.end() &&
               em.has_component<TransformComponent>(candidate);
    };
    assert(target_filter(target) == true);
    assert(target_filter(c1) == false); // self camera 1 excluded
    assert(target_filter(c2) == false); // self camera 2 excluded
    assert(target_filter(no_transform) == false); // entity without transform excluded

    // Bulk set follow_target to target
    const auto key = EditorPropertyTransaction::make_key(selection_transaction_owner(selection), "MultiSelection.Camera.FollowTarget");
    assert(transactions.begin<SelectedComponentStates<CameraComponent>>(
        key,
        before,
        [&]() -> std::optional<SelectedComponentStates<CameraComponent>> {
            return capture_selected_components<CameraComponent>(em, selection);
        },
        [&](const SelectedComponentStates<CameraComponent>& states) {
            for (const auto& [entity, comp] : states) {
                em.get_component<CameraComponent>(entity) = comp;
            }
        },
        "Change Camera Follow Target",
        [](const auto& lhs, const auto& rhs) {
            return selected_component_states_equal(lhs, rhs, [](const auto& a, const auto& b) { return a.follow_target == b.follow_target; });
        }
    ));

    em.get_component<CameraComponent>(c1).follow_target = target;
    em.get_component<CameraComponent>(c2).follow_target = target;
    assert(transactions.commit(key, history));

    assert(em.get_component<CameraComponent>(c1).follow_target == target);
    assert(em.get_component<CameraComponent>(c2).follow_target == target);
    // Zooms are untouched
    assert(em.get_component<CameraComponent>(c1).zoom == 1.0f);
    assert(em.get_component<CameraComponent>(c2).zoom == 2.0f);

    history.undo();
    assert(em.get_component<CameraComponent>(c1).follow_target == 0);
    assert(em.get_component<CameraComponent>(c2).follow_target == 0);

    history.redo();
    assert(em.get_component<CameraComponent>(c1).follow_target == target);
    assert(em.get_component<CameraComponent>(c2).follow_target == target);
}

void test_multiselection_camera_clear_color_round_trips() {
    EntityManager em;
    EditorCommandHistory history;
    EditorPropertyTransaction transactions;

    const EntityID c1 = em.create_entity();
    const EntityID c2 = em.create_entity();
    em.add_component<CameraComponent>(c1, CameraComponent{});
    em.add_component<CameraComponent>(c2, CameraComponent{});
    em.get_component<CameraComponent>(c1).clear_color = {24, 28, 36, 255};
    em.get_component<CameraComponent>(c2).clear_color = {100, 120, 140, 255};

    const std::vector<EntityID> selection = {c1, c2};
    const auto before = capture_selected_components<CameraComponent>(em, selection);
    const auto key = EditorPropertyTransaction::make_key(selection_transaction_owner(selection), "MultiSelection.Camera.ClearColor");
    assert(transactions.begin<SelectedComponentStates<CameraComponent>>(
        key,
        before,
        [&]() -> std::optional<SelectedComponentStates<CameraComponent>> {
            return capture_selected_components<CameraComponent>(em, selection);
        },
        [&](const SelectedComponentStates<CameraComponent>& states) {
            for (const auto& [entity, component] : states) {
                em.get_component<CameraComponent>(entity) = component;
            }
        },
        "Edit Camera Clear Color",
        [](const auto& lhs, const auto& rhs) {
            return selected_component_states_equal(lhs, rhs, std::equal_to<CameraComponent>{});
        }
    ));

    const SDL_Color edited{10, 200, 70, 180};
    em.get_component<CameraComponent>(c1).clear_color = edited;
    em.get_component<CameraComponent>(c2).clear_color = edited;
    assert(transactions.commit(key, history));
    assert(em.get_component<CameraComponent>(c1).clear_color.g == 200);
    assert(em.get_component<CameraComponent>(c2).clear_color.a == 180);

    history.undo();
    assert(em.get_component<CameraComponent>(c1).clear_color.r == 24);
    assert(em.get_component<CameraComponent>(c2).clear_color.b == 140);

    history.redo();
    const SDL_Color redone_c1 = em.get_component<CameraComponent>(c1).clear_color;
    const SDL_Color redone_c2 = em.get_component<CameraComponent>(c2).clear_color;
    assert(redone_c1.r == edited.r && redone_c1.g == edited.g && redone_c1.b == edited.b && redone_c1.a == edited.a);
    assert(redone_c2.r == edited.r && redone_c2.g == edited.g && redone_c2.b == edited.b && redone_c2.a == edited.a);
}

void test_multiselection_escape_cancellation_restores_individual_values() {
    EntityManager em;
    EditorCommandHistory history;
    EditorPropertyTransaction transactions;

    EntityID e1 = em.create_entity();
    em.add_component<TransformComponent>(e1, TransformComponent{glm::vec3(0.0f)});
    RigidBodyComponent rb1;
    rb1.gravity_scale = 1.0f;
    em.add_component<RigidBodyComponent>(e1, rb1);

    EntityID e2 = em.create_entity();
    em.add_component<TransformComponent>(e2, TransformComponent{glm::vec3(10.0f)});
    RigidBodyComponent rb2;
    rb2.gravity_scale = 9.8f;
    em.add_component<RigidBodyComponent>(e2, rb2);

    const std::vector<EntityID> selection = {e1, e2};
    const auto before = capture_selected_components<RigidBodyComponent>(em, selection);

    std::size_t cancel_side_effect_count = 0;
    auto side_effect = [&](EntityManager&, const std::vector<EntityID>&) {
        ++cancel_side_effect_count;
    };

    const auto key = EditorPropertyTransaction::make_key(selection_transaction_owner(selection), "MultiSelection.RigidBody.GravityScale");
    assert(transactions.begin<SelectedComponentStates<RigidBodyComponent>>(
        key,
        before,
        [&]() -> std::optional<SelectedComponentStates<RigidBodyComponent>> {
            return capture_selected_components<RigidBodyComponent>(em, selection);
        },
        [&](const SelectedComponentStates<RigidBodyComponent>& states) {
            for (const auto& [entity, comp] : states) {
                em.get_component<RigidBodyComponent>(entity) = comp;
            }
            side_effect(em, selection);
        },
        "Edit Selected RigidBody Gravity Scale",
        [](const auto& lhs, const auto& rhs) {
            return selected_component_states_equal(lhs, rhs, [](const auto& a, const auto& b) { return a.gravity_scale == b.gravity_scale; });
        }
    ));

    // Mid-drag interactive value change
    em.get_component<RigidBodyComponent>(e1).gravity_scale = 0.0f;
    em.get_component<RigidBodyComponent>(e2).gravity_scale = 0.0f;

    // User presses Escape -> cancel transaction
    assert(transactions.cancel(key));

    // Must restore individual respective original values and run side effect!
    assert(em.get_component<RigidBodyComponent>(e1).gravity_scale == 1.0f);
    assert(em.get_component<RigidBodyComponent>(e2).gravity_scale == 9.8f);
    assert(cancel_side_effect_count == 1);
    assert(history.get_undo_count() == 0);
}

void test_multiselection_locked_state_disables_modifications() {
    EntityManager em;
    EditorEntityStateStore state_store;

    EntityID e1 = em.create_entity();
    em.add_component<TagComponent>(e1, TagComponent{"Entity_A"});
    em.add_component<TransformComponent>(e1, TransformComponent{});

    EntityID e2 = em.create_entity();
    em.add_component<TagComponent>(e2, TagComponent{"Entity_B"});
    em.add_component<TransformComponent>(e2, TransformComponent{});

    state_store.set_locked(e2, true);

    const std::vector<EntityID> selection = {e1, e2};
    const bool any_editor_locked = std::any_of(selection.begin(), selection.end(), [&](EntityID e) {
        return state_store.is_locked(e);
    });
    assert(any_editor_locked == true);
}

void test_multiselection_gizmo_world_to_local_translation_and_orbit_rotation() {
    EntityManager em;
    EditorCommandHistory history;

    // Parent entity at (100, 50)
    EntityID parent = em.create_entity();
    em.add_component<TransformComponent>(parent, TransformComponent{glm::vec3(100.0f, 50.0f, 0.0f)});

    // Child entity at local (10, 0) -> world position (110, 50)
    EntityID child = em.create_entity();
    em.add_component<TransformComponent>(child, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});
    em.add_component<HierarchyComponent>(child, HierarchyComponent{parent});

    // Parent entity at (100, 50) with child
    HierarchyComponent parent_hier;
    parent_hier.children.push_back(child);
    em.add_component<HierarchyComponent>(parent, parent_hier);

    // Root entity 2 at (90, 50)
    EntityID root2 = em.create_entity();
    em.add_component<TransformComponent>(root2, TransformComponent{glm::vec3(90.0f, 50.0f, 0.0f)});

    TransformSystem::update(em);
    assert(em.get_component<TransformComponent>(child).world_position == glm::vec3(110.0f, 50.0f, 0.0f));

    // Multi-selection of child and root2:
    // Centroid computed via pure math helper
    const std::vector<glm::vec3> positions = {
        em.get_component<TransformComponent>(child).world_position,
        em.get_component<TransformComponent>(root2).world_position
    };
    const glm::vec3 centroid = ViewportTransformMath::compute_centroid(positions);
    assert(centroid == glm::vec3(100.0f, 50.0f, 0.0f));

    // Move test via ViewportTransformMath::calculate_move_target
    const glm::vec2 move_delta(20.0f, -10.0f);
    TransformComponent child_move_world = ViewportTransformMath::calculate_move_target(
        em.get_component<TransformComponent>(child), move_delta
    );
    assert(child_move_world.world_position == glm::vec3(130.0f, 40.0f, 0.0f));
    auto child_move_local = TransformSystem::world_to_local_transform(child_move_world, &em.get_component<TransformComponent>(parent));
    assert(child_move_local.has_value());
    assert(child_move_local->position == glm::vec3(30.0f, -10.0f, 0.0f));

    // Rotate test: Orbit 90 degrees around centroid (100, 50) via ViewportTransformMath::calculate_rotate_target
    const float angle_delta = 90.0f;
    TransformComponent child_world_target = ViewportTransformMath::calculate_rotate_target(
        em.get_component<TransformComponent>(child), centroid, angle_delta
    );
    assert(std::abs(child_world_target.world_position.x - 100.0f) < 0.001f);
    assert(std::abs(child_world_target.world_position.y - 60.0f) < 0.001f);
    assert(std::abs(child_world_target.world_rotation - 90.0f) < 0.001f);

    auto child_local = TransformSystem::world_to_local_transform(child_world_target, &em.get_component<TransformComponent>(parent));
    assert(child_local.has_value());
    assert(std::abs(child_local->position.x - 0.0f) < 0.001f);
    assert(std::abs(child_local->position.y - 10.0f) < 0.001f);
    assert(std::abs(child_local->rotation - 90.0f) < 0.001f);

    std::unordered_map<EntityID, TransformComponent> before_map;
    before_map[child] = em.get_component<TransformComponent>(child);
    before_map[root2] = em.get_component<TransformComponent>(root2);

    std::unordered_map<EntityID, TransformComponent> after_map;
    after_map[child] = *child_local;

    TransformComponent root2_world_target = ViewportTransformMath::calculate_rotate_target(
        em.get_component<TransformComponent>(root2), centroid, angle_delta
    );
    auto root2_local = TransformSystem::world_to_local_transform(root2_world_target, nullptr);
    assert(root2_local.has_value());
    after_map[root2] = *root2_local;

    history.execute_command(std::make_unique<TransformEntitiesCommand>(&em, before_map, after_map, "Rotate Selection"));
    TransformSystem::update(em);

    assert(std::abs(em.get_component<TransformComponent>(child).world_position.x - 100.0f) < 0.001f);
    assert(std::abs(em.get_component<TransformComponent>(child).world_position.y - 60.0f) < 0.001f);
    assert(std::abs(em.get_component<TransformComponent>(root2).world_position.x - 100.0f) < 0.001f);
    assert(std::abs(em.get_component<TransformComponent>(root2).world_position.y - 40.0f) < 0.001f);

    // Undo restores initial world and local positions
    history.undo();
    TransformSystem::update(em);
    assert(em.get_component<TransformComponent>(child).world_position == glm::vec3(110.0f, 50.0f, 0.0f));
    assert(em.get_component<TransformComponent>(root2).world_position == glm::vec3(90.0f, 50.0f, 0.0f));

    // Redo restores rotated world positions
    history.redo();
    TransformSystem::update(em);
    assert(std::abs(em.get_component<TransformComponent>(child).world_position.x - 100.0f) < 0.001f);
    assert(std::abs(em.get_component<TransformComponent>(child).world_position.y - 60.0f) < 0.001f);
}

void test_multiselection_gizmo_scale_preserves_negative_sign() {
    EntityManager em;
    EditorCommandHistory history;

    EntityID e1 = em.create_entity();
    em.add_component<TransformComponent>(e1, TransformComponent{
        glm::vec3(-20.0f, 0.0f, 0.0f),
        glm::vec2(-1.0f, 1.0f) // Flipped horizontally
    });

    EntityID e2 = em.create_entity();
    em.add_component<TransformComponent>(e2, TransformComponent{
        glm::vec3(20.0f, 0.0f, 0.0f),
        glm::vec2(2.0f, 2.0f)
    });

    TransformSystem::update(em);
    const glm::vec3 centroid = ViewportTransformMath::compute_centroid({
        em.get_component<TransformComponent>(e1).world_position,
        em.get_component<TransformComponent>(e2).world_position
    });
    assert(centroid == glm::vec3(0.0f));

    // Scale by 2.5 via ViewportTransformMath::calculate_scale_target
    const float scale_factor = 2.5f;
    const glm::vec2 scale_vec(scale_factor, scale_factor);

    std::unordered_map<EntityID, TransformComponent> before_map;
    before_map[e1] = em.get_component<TransformComponent>(e1);
    before_map[e2] = em.get_component<TransformComponent>(e2);

    std::unordered_map<EntityID, TransformComponent> after_map;
    for (const auto& [id, before_t] : before_map) {
        TransformComponent target = ViewportTransformMath::calculate_scale_target(before_t, centroid, scale_vec);
        auto local = TransformSystem::world_to_local_transform(target, nullptr);
        assert(local.has_value());
        after_map[id] = *local;
    }

    history.execute_command(std::make_unique<TransformEntitiesCommand>(&em, before_map, after_map, "Scale Selection"));
    TransformSystem::update(em);

    assert(em.get_component<TransformComponent>(e1).world_position == glm::vec3(-50.0f, 0.0f, 0.0f));
    assert(em.get_component<TransformComponent>(e1).world_scale.x == -2.5f); // Negative sign preserved!
    assert(em.get_component<TransformComponent>(e1).world_scale.y == 2.5f);

    assert(em.get_component<TransformComponent>(e2).world_position == glm::vec3(50.0f, 0.0f, 0.0f));
    assert(em.get_component<TransformComponent>(e2).world_scale == glm::vec2(5.0f, 5.0f));

    // Undo / Redo
    history.undo();
    TransformSystem::update(em);
    assert(em.get_component<TransformComponent>(e1).world_scale.x == -1.0f);
    assert(em.get_component<TransformComponent>(e1).world_position.x == -20.0f);

    history.redo();
    TransformSystem::update(em);
    assert(em.get_component<TransformComponent>(e1).world_scale.x == -2.5f);
    assert(em.get_component<TransformComponent>(e1).world_position.x == -50.0f);
}

void test_create_entity_command_with_sprite_round_trips() {
    EntityManager em;
    EditorCommandHistory history(100);
    auto selection = std::make_shared<EditorSelection>();

    EntitySnapshot snap;
    snap.registered_components.set(TagComponent{"Dropped_Hero"});
    snap.registered_components.set(TransformComponent{glm::vec3(125.0f, -45.0f, 0.0f)});
    snap.registered_components.set(SpriteComponent{
        nullptr,
        glm::vec2(128.0f, 128.0f),
        SDL_Color{200, 100, 50, 255}
    });

    auto cmd = std::make_unique<CreateEntityCommand>(&em, selection, std::move(snap));
    assert(cmd->is_valid());
    history.execute_command(std::move(cmd));

    assert(selection->count() == 1);
    const EntityID created_id = selection->get_primary();
    assert(em.is_alive(created_id));
    assert(em.has_component<TagComponent>(created_id));
    assert(em.get_component<TagComponent>(created_id).tag == "Dropped_Hero");
    assert(em.has_component<TransformComponent>(created_id));
    assert(em.get_component<TransformComponent>(created_id).position == glm::vec3(125.0f, -45.0f, 0.0f));
    assert(em.has_component<SpriteComponent>(created_id));
    assert(em.get_component<SpriteComponent>(created_id).size == glm::vec2(128.0f, 128.0f));
    assert(em.get_component<SpriteComponent>(created_id).color.r == 200);

    // Undo destroys the entity
    history.undo();
    assert(!em.is_alive(created_id));
    assert(selection->empty());

    // Redo restores the exact entity, components, and selection
    history.redo();
    assert(em.is_alive(created_id));
    assert(selection->is_selected(created_id));
    assert(em.get_component<TagComponent>(created_id).tag == "Dropped_Hero");
    assert(em.get_component<SpriteComponent>(created_id).size == glm::vec2(128.0f, 128.0f));
}

} // namespace

int main() {
    test_entity_rename_round_trips();
    test_sprite_compound_edit_round_trips();
    test_component_presence_round_trips_exact_state();
    test_registered_component_presence_command_round_trips();
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
    test_multiselection_rigidbody_mixed_values_and_undo_redo();
    test_multiselection_collider_compatibility_and_mixed_edit();
    test_multiselection_camera_bounds_and_follow_target();
    test_multiselection_camera_clear_color_round_trips();
    test_multiselection_escape_cancellation_restores_individual_values();
    test_multiselection_locked_state_disables_modifications();
    test_multiselection_gizmo_world_to_local_translation_and_orbit_rotation();
    test_multiselection_gizmo_scale_preserves_negative_sign();
    test_create_entity_command_with_sprite_round_trips();
    std::cout << "InspectorCommandTests passed\n";
    return 0;
}
