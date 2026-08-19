#include "core/FileSystem.hpp"
#include "ecs/components/AnimationComponents.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/PhysicsComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "scene/Scene.hpp"
#include "scene/ComponentRegistry.hpp"
#include "scene/SceneSerializer.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {

using namespace Engine;

class TestScene final : public Scene {
public:
    TestScene() : Scene("HierarchySnapshotTest") {}
};

EntityID add_preserved_entity(TestScene& scene) {
    EntityManager& em = scene.get_entity_manager();
    const EntityID entity = em.create_entity();
    em.add_component<TransformComponent>(entity, TransformComponent{});
    em.add_component<TagComponent>(entity, TagComponent{"MustSurvive"});
    return entity;
}

void assert_preserved(const TestScene& scene, EntityID entity) {
    const EntityManager& em = scene.get_entity_manager();
    assert(em.is_alive(entity));
    assert(em.has_component<TagComponent>(entity));
    assert(em.get_component<TagComponent>(entity).tag == "MustSurvive");
    assert(em.get_living_entities().size() == 1);
}

void test_round_trip_restores_destroyed_child_hierarchy() {
    TestScene scene;
    EntityManager& em = scene.get_entity_manager();

    const EntityID parent = em.create_entity();
    em.add_component<TagComponent>(parent, TagComponent{"Coins"});
    em.add_component<TransformComponent>(parent, TransformComponent{});

    const EntityID coin = em.create_entity();
    em.add_component<TagComponent>(coin, TagComponent{"Coin"});
    em.add_component<TransformComponent>(coin, TransformComponent{glm::vec3(32.0f, 0.0f, 0.0f)});
    em.set_parent(coin, parent);

    SceneSerializer serializer(&scene);
    const std::string authoring_snapshot = serializer.serialize_to_string();
    assert(!authoring_snapshot.empty());

    // Reproduce collecting the coin during Play. EntityManager removes the
    // runtime child link as part of destruction.
    em.destroy_entity(coin);
    assert(em.get_component<HierarchyComponent>(parent).children.empty());

    assert(serializer.deserialize_from_string(authoring_snapshot));
    assert(em.is_alive(parent));
    assert(em.is_alive(coin));
    assert(em.has_component<HierarchyComponent>(parent));
    assert(em.has_component<HierarchyComponent>(coin));
    assert(em.get_component<HierarchyComponent>(coin).parent == parent);

    const auto& children = em.get_component<HierarchyComponent>(parent).children;
    assert(children.size() == 1);
    assert(children.front() == coin);
}

void test_forward_parent_reference_is_order_independent() {
    TestScene scene;
    EntityManager& em = scene.get_entity_manager();

    // Create the child first so its serialized record precedes its parent.
    const EntityID child = em.create_entity();
    em.add_component<TransformComponent>(child, TransformComponent{});
    const EntityID parent = em.create_entity();
    em.add_component<TransformComponent>(parent, TransformComponent{});
    em.set_parent(child, parent);

    SceneSerializer serializer(&scene);
    const std::string snapshot = serializer.serialize_to_string();
    assert(serializer.deserialize_from_string(snapshot));
    assert(em.get_component<HierarchyComponent>(child).parent == parent);
    assert(em.get_component<HierarchyComponent>(parent).children == std::vector<EntityID>{child});
}

void test_sprite_outline_authoring_round_trips() {
    TestScene scene;
    EntityManager& em = scene.get_entity_manager();
    const EntityID entity = em.create_entity();
    em.add_component<TransformComponent>(entity, TransformComponent{});
    SpriteComponent sprite;
    sprite.has_outline = true;
    sprite.outline_color = {12, 34, 56, 78};
    sprite.outline_thickness = 4.5f;
    sprite.outline_pulse = true;
    em.add_component<SpriteComponent>(entity, sprite);

    SceneSerializer serializer(&scene);
    const std::string snapshot = serializer.serialize_to_string();
    em.get_component<SpriteComponent>(entity).has_outline = false;

    assert(serializer.deserialize_from_string(snapshot));
    const auto& restored = em.get_component<SpriteComponent>(entity);
    assert(restored.has_outline);
    assert(restored.outline_color.r == 12);
    assert(restored.outline_color.g == 34);
    assert(restored.outline_color.b == 56);
    assert(restored.outline_color.a == 78);
    assert(restored.outline_thickness == 4.5f);
    assert(restored.outline_pulse);
}

void test_all_project_scenes_match_the_registered_schema() {
    const std::string scenes_directory = std::string(ENGINE_SOURCE_DIR) + "/assets/scenes";
    const auto scene_files = FileSystem::list_files(scenes_directory, ".scene");
    assert(!scene_files.empty());
    for (const std::string& scene_file : scene_files) {
        TestScene scene;
        SceneSerializer serializer(&scene);
        const std::string contents = FileSystem::read_text(scene_file);
        assert(!contents.empty());
        assert(serializer.deserialize_from_string(contents));
    }
}

void test_serialization_is_deterministic_and_excludes_runtime_state() {
    TestScene scene;
    EntityManager& em = scene.get_entity_manager();
    const EntityID entity = em.create_entity();
    em.add_component<TransformComponent>(entity, TransformComponent{});
    em.add_component<TagComponent>(entity, TagComponent{"Deterministic"});

    RigidBodyComponent body = RigidBodyComponent::Dynamic();
    body.velocity = {80.0f, -15.0f};
    body.angular_velocity = 25.0f;
    em.add_component<RigidBodyComponent>(entity, body);
    em.add_component<ColliderComponent>(entity, ColliderComponent::Box({32.0f, 16.0f}));

    AnimationComponent animation;
    animation.add_clip("Idle", 0, 4, 8.0f, true);
    em.add_component<AnimationComponent>(entity, animation);
    AnimationPlaybackStateComponent playback;
    playback.current_frame = 3;
    playback.timer = 0.75f;
    playback.initialized = true;
    em.add_component<AnimationPlaybackStateComponent>(entity, playback);

    SceneSerializer serializer(&scene);
    const std::string first = serializer.serialize_to_string();
    const std::string second = serializer.serialize_to_string();
    assert(first == second);

    const auto root = nlohmann::json::parse(first);
    const auto& serialized_entity = root.at("entities").front();
    assert(!serialized_entity.at("rigidbody").contains("velocity"));
    assert(!serialized_entity.at("rigidbody").contains("angular_velocity"));
    assert(!serialized_entity.contains("animation_playback"));

    assert(serializer.deserialize_from_string(first));
    assert(em.get_component<RigidBodyComponent>(entity).velocity == glm::vec2(0.0f));
    assert(em.get_component<RigidBodyComponent>(entity).angular_velocity == 0.0f);
    assert(!em.has_component<AnimationPlaybackStateComponent>(entity));
}

void test_failed_load_is_transactional() {
    TestScene scene;
    const EntityID preserved = add_preserved_entity(scene);
    SceneSerializer serializer(&scene);
    nlohmann::json root = nlohmann::json::parse(serializer.serialize_to_string());

    root["entities"].front()["unknown_component"] = nlohmann::json::object();
    assert(!serializer.deserialize_from_string(root.dump()));
    assert_preserved(scene, preserved);

    root = nlohmann::json::parse(serializer.serialize_to_string());
    root["entities"].front()["hierarchy"] = {{"parent", 9999}};
    assert(!serializer.deserialize_from_string(root.dump()));
    assert_preserved(scene, preserved);

    root = nlohmann::json::parse(serializer.serialize_to_string());
    root["entities"].push_back(root["entities"].front());
    assert(!serializer.deserialize_from_string(root.dump()));
    assert_preserved(scene, preserved);
}

void test_future_scene_version_is_rejected_transactionally() {
    TestScene scene;
    const EntityID preserved = add_preserved_entity(scene);
    SceneSerializer serializer(&scene);
    nlohmann::json root = nlohmann::json::parse(serializer.serialize_to_string());
    root["version"] = 999;

    assert(!serializer.deserialize_from_string(root.dump()));
    assert_preserved(scene, preserved);
}

void test_v1_scene_migrates_to_current_schema() {
    TestScene source;
    add_preserved_entity(source);
    SceneSerializer source_serializer(&source);
    nlohmann::json legacy = nlohmann::json::parse(source_serializer.serialize_to_string());
    legacy["version"] = 1;
    legacy.erase("component_versions");

    TestScene loaded;
    SceneSerializer loaded_serializer(&loaded);
    assert(loaded_serializer.deserialize_from_string(legacy.dump()));
    const nlohmann::json current = nlohmann::json::parse(loaded_serializer.serialize_to_string());
    assert(current.at("version").get<std::uint32_t>() == 2);
    assert(current.at("component_versions").is_object());
    assert(current.at("component_versions").size() == default_component_registry().descriptors().size());
}

void test_hierarchy_cycle_is_rejected_transactionally() {
    TestScene scene;
    const EntityID preserved = add_preserved_entity(scene);
    SceneSerializer serializer(&scene);
    nlohmann::json root = nlohmann::json::parse(serializer.serialize_to_string());
    auto second = root["entities"].front();
    second["id"] = preserved + 1;
    root["entities"].front()["hierarchy"] = {{"parent", preserved + 1}};
    second["hierarchy"] = {{"parent", preserved}};
    root["entities"].push_back(std::move(second));

    assert(!serializer.deserialize_from_string(root.dump()));
    assert_preserved(scene, preserved);
}

void test_commit_failure_rolls_back_live_scene() {
    ComponentRegistry registry;
    TypedComponentRegistration<TransformComponent> registration{
        .display_name = "Transform",
        .serialized_key = "transform",
        .schema_version = 1,
        .required = true,
        .serialize = [](const TransformComponent&, const ComponentSerializationContext&) {
            return nlohmann::json::object();
        },
        .deserialize = [](
            TransformComponent&,
            const nlohmann::json&,
            const ComponentSerializationContext&,
            std::string&
        ) { return true; },
        .finalize = [](
            EntityManager&,
            EntityID,
            TransformComponent&,
            const ComponentFinalizeContext& context,
            std::string& error
        ) {
            if (context.reason == ComponentFinalizeReason::DeserializationCommit) {
                error = "forced commit failure";
                return false;
            }
            return true;
        }
    };
    std::string registration_error;
    assert(registry.register_component(std::move(registration), &registration_error));

    TestScene scene;
    const EntityID preserved = add_preserved_entity(scene);
    SceneSerializer serializer(&scene, nullptr, &registry);
    const std::string candidate = serializer.serialize_to_string();
    scene.get_entity_manager().get_component<TransformComponent>(preserved).position.x = 42.0f;

    assert(!serializer.deserialize_from_string(candidate));
    assert_preserved(scene, preserved);
    assert(scene.get_entity_manager().get_component<TransformComponent>(preserved).position.x == 42.0f);
}

} // namespace

int main() {
    test_round_trip_restores_destroyed_child_hierarchy();
    test_forward_parent_reference_is_order_independent();
    test_sprite_outline_authoring_round_trips();
    test_all_project_scenes_match_the_registered_schema();
    test_serialization_is_deterministic_and_excludes_runtime_state();
    test_failed_load_is_transactional();
    test_future_scene_version_is_rejected_transactionally();
    test_v1_scene_migrates_to_current_schema();
    test_hierarchy_cycle_is_rejected_transactionally();
    test_commit_failure_rolls_back_live_scene();
    std::cout << "Scene serializer tests passed\n";
    return 0;
}
