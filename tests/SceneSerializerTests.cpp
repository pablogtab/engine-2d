#include "core/FileSystem.hpp"
#include "core/AssetDatabase.hpp"
#include "ecs/components/AnimationComponents.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/PhysicsComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "scene/Scene.hpp"
#include "scene/ComponentRegistry.hpp"
#include "scene/SceneSerializer.hpp"
#include "editor/commands/EditorCommandHistory.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {

using namespace Engine;

class TestScene final : public Scene {
public:
    TestScene() : Scene("HierarchySnapshotTest") {}
    [[nodiscard]] bool supports_render_layer_effects() const override { return true; }
    [[nodiscard]] const std::vector<RenderLayerEffectAssignment>& get_render_layer_effect_assignments() const override { return layer_effect_assignments; }
    bool set_render_layer_effect_assignments(std::vector<RenderLayerEffectAssignment> assignments, const AssetDatabase&) override {
        layer_effect_assignments = std::move(assignments); return true;
    }
    std::vector<RenderLayerEffectAssignment> layer_effect_assignments;
};

void test_render_layer_effect_assignments_round_trip() {
    TestScene scene;
    scene.layer_effect_assignments = {{AssetHandle{10}, 2, true}, {AssetHandle{10}, 9, false}};
    SceneSerializer serializer(&scene);
    const std::string snapshot = serializer.serialize_to_string();
    scene.layer_effect_assignments.clear();
    assert(serializer.deserialize_from_string(snapshot));
    assert(scene.layer_effect_assignments == std::vector<RenderLayerEffectAssignment>(
        {{AssetHandle{10}, 2, true}, {AssetHandle{10}, 9, false}}
    ));
}

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
    assert(em.set_parent(coin, parent).succeeded());

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
    assert(em.set_parent(child, parent).succeeded());

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

void test_discard_changes_reloads_saved_scene_file() {
    const std::string temp_path = "assets/scenes/temp_discard_test.scene";
    std::filesystem::create_directories("assets/scenes");

    // 1. Create a scene with 1 entity and serialize it to disk
    TestScene scene;
    EntityManager& em = scene.get_entity_manager();
    const EntityID original_entity = em.create_entity();
    em.add_component<TagComponent>(original_entity, TagComponent{"SavedEntity"});
    em.add_component<TransformComponent>(original_entity, TransformComponent{glm::vec3(10.0f, 20.0f, 0.0f)});

    SceneSerializer serializer(&scene);
    assert(serializer.serialize(temp_path));

    // 2. Simulate editor session modifying the scene
    EditorCommandHistory history;
    assert(!history.is_dirty());
    history.mark_saved();

    // Modify scene in memory: change transform and add extra entity
    em.get_component<TransformComponent>(original_entity).position.x = 999.0f;
    const EntityID dirty_entity = em.create_entity();
    em.add_component<TagComponent>(dirty_entity, TagComponent{"UnsavedEntity"});
    em.add_component<TransformComponent>(dirty_entity, TransformComponent{});
    assert(em.get_living_entities().size() == 2);

    // 3. Perform Discard: reload from file and reset baseline
    assert(serializer.deserialize(temp_path));
    history.reset_clean_baseline(0);

    // 4. Verify scene is restored and clean
    assert(!history.is_dirty());
    const auto& living = em.get_living_entities();
    assert(living.size() == 1);
    assert(em.is_alive(original_entity));
    assert(em.get_component<TagComponent>(original_entity).tag == "SavedEntity");
    assert(em.get_component<TransformComponent>(original_entity).position.x == 10.0f);
    assert(em.get_component<TransformComponent>(original_entity).position.y == 20.0f);

    std::filesystem::remove(temp_path);
}

void test_discard_changes_restores_initial_snapshot_for_new_scene() {
    // 1. Create a new blank scene and capture its initial snapshot
    TestScene scene;
    EntityManager& em = scene.get_entity_manager();
    const EntityID camera_entity = em.create_entity();
    em.add_component<TagComponent>(camera_entity, TagComponent{"Main Camera"});
    em.add_component<TransformComponent>(camera_entity, TransformComponent{glm::vec3(0.0f)});

    SceneSerializer serializer(&scene);
    const std::string initial_snapshot = serializer.serialize_to_string();
    assert(!initial_snapshot.empty());

    EditorCommandHistory history;
    history.reset_clean_baseline(0);
    assert(!history.is_dirty());

    // 2. Dirty the new scene by adding entities
    const EntityID new_entity = em.create_entity();
    em.add_component<TagComponent>(new_entity, TagComponent{"TemporaryTilemap"});
    em.add_component<TransformComponent>(new_entity, TransformComponent{glm::vec3(50.0f, 50.0f, 0.0f)});
    assert(em.get_living_entities().size() == 2);

    // 3. Perform Discard: restore initial snapshot string
    assert(serializer.deserialize_from_string(initial_snapshot));
    history.reset_clean_baseline(0);

    // 4. Verify new scene is back to initial state and clean
    assert(!history.is_dirty());
    const auto& living = em.get_living_entities();
    assert(living.size() == 1);
    assert(em.is_alive(camera_entity));
    assert(em.get_component<TagComponent>(camera_entity).tag == "Main Camera");
    assert(!em.is_alive(new_entity));
}

void test_scene_with_group_component_round_trip() {
    TestScene source;
    EntityManager& src_em = source.get_entity_manager();

    const EntityID group_id = src_em.create_entity();
    src_em.add_component<TagComponent>(group_id, TagComponent{"ObstaclesGroup"});
    src_em.add_component<TransformComponent>(group_id, TransformComponent{glm::vec3(100.0f, 200.0f, 0.0f)});
    src_em.add_component<GroupComponent>(group_id, GroupComponent{});

    const EntityID child1 = src_em.create_entity();
    src_em.add_component<TagComponent>(child1, TagComponent{"Box1"});
    src_em.add_component<TransformComponent>(child1, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});
    assert(src_em.set_parent(child1, group_id).succeeded());

    const EntityID child2 = src_em.create_entity();
    src_em.add_component<TagComponent>(child2, TagComponent{"Box2"});
    src_em.add_component<TransformComponent>(child2, TransformComponent{glm::vec3(-10.0f, 0.0f, 0.0f)});
    assert(src_em.set_parent(child2, group_id).succeeded());

    SceneSerializer serializer(&source);
    const std::string serialized = serializer.serialize_to_string();
    assert(!serialized.empty());

    const nlohmann::json json = nlohmann::json::parse(serialized);
    assert(json.contains("entities"));

    TestScene target;
    SceneSerializer target_serializer(&target);
    assert(target_serializer.deserialize_from_string(serialized));

    EntityManager& tgt_em = target.get_entity_manager();
    assert(tgt_em.get_living_entities().size() == 3);

    assert(tgt_em.is_alive(group_id));
    assert(tgt_em.has_component<GroupComponent>(group_id));
    assert(tgt_em.get_component<TagComponent>(group_id).tag == "ObstaclesGroup");
    assert(tgt_em.has_component<HierarchyComponent>(group_id));
    const auto& children = tgt_em.get_component<HierarchyComponent>(group_id).children;
    assert(children.size() == 2);
    assert(std::find(children.begin(), children.end(), child1) != children.end());
    assert(std::find(children.begin(), children.end(), child2) != children.end());
}

void test_entity_lock_and_editor_visibility_are_not_serialized() {
    TestScene source;
    EntityManager& src_em = source.get_entity_manager();

    const EntityID e1 = src_em.create_entity();
    src_em.add_component<TagComponent>(e1, TagComponent{"TestLocked"});
    src_em.add_component<TransformComponent>(e1, TransformComponent{});

    SceneSerializer serializer(&source);
    const std::string serialized = serializer.serialize_to_string();
    assert(!serialized.empty());

    // Verify raw JSON does not contain locked or visible_in_editor
    assert(serialized.find("\"locked\"") == std::string::npos);
    assert(serialized.find("\"visible_in_editor\"") == std::string::npos);
    assert(serialized.find("\"editor_visible\"") == std::string::npos);
}

void test_component_order_editor_metadata_round_trips_and_is_optional() {
    TestScene source;
    EntityManager& src_em = source.get_entity_manager();
    const EntityID entity = src_em.create_entity();
    src_em.add_component<TagComponent>(entity, TagComponent{"Ordered"});
    src_em.add_component<TransformComponent>(entity, TransformComponent{});
    src_em.add_component<SpriteComponent>(entity, SpriteComponent{});

    SceneEditorMetadata source_metadata;
    source_metadata.component_orders[entity] = {"sprite", "transform"};
    SceneSerializer serializer(&source, nullptr, nullptr, &source_metadata);
    const std::string serialized = serializer.serialize_to_string();
    assert(!serialized.empty());
    const nlohmann::json root = nlohmann::json::parse(serialized);
    assert(root.at("editor_metadata").at("component_orders").size() == 1);

    TestScene loaded_with_metadata;
    SceneEditorMetadata loaded_metadata;
    SceneSerializer loaded_serializer(&loaded_with_metadata, nullptr, nullptr, &loaded_metadata);
    assert(loaded_serializer.deserialize_from_string(serialized));
    assert((loaded_metadata.component_orders.at(entity) ==
        std::vector<std::string>{"sprite", "transform"}));

    TestScene runtime_only;
    SceneSerializer runtime_serializer(&runtime_only);
    assert(runtime_serializer.deserialize_from_string(serialized));
    assert(runtime_only.get_entity_manager().is_alive(entity));
}

} // namespace

int main() {
    std::filesystem::current_path(ENGINE_SOURCE_DIR);
    [[maybe_unused]] const AssetScanResult assets = AssetDatabase::get().scan("assets");
    test_round_trip_restores_destroyed_child_hierarchy();
    test_render_layer_effect_assignments_round_trip();
    test_forward_parent_reference_is_order_independent();
    test_sprite_outline_authoring_round_trips();
    test_all_project_scenes_match_the_registered_schema();
    test_serialization_is_deterministic_and_excludes_runtime_state();
    test_failed_load_is_transactional();
    test_future_scene_version_is_rejected_transactionally();
    test_v1_scene_migrates_to_current_schema();
    test_hierarchy_cycle_is_rejected_transactionally();
    test_commit_failure_rolls_back_live_scene();
    test_discard_changes_reloads_saved_scene_file();
    test_discard_changes_restores_initial_snapshot_for_new_scene();
    test_scene_with_group_component_round_trip();
    test_entity_lock_and_editor_visibility_are_not_serialized();
    test_component_order_editor_metadata_round_trips_and_is_optional();
    std::cout << "Scene serializer tests passed\n";
    return 0;
}
