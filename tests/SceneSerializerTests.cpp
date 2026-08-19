#include "core/FileSystem.hpp"
#include "ecs/Components.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneSerializer.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>

namespace {

using namespace Engine;

class TestScene final : public Scene {
public:
    TestScene() : Scene("HierarchySnapshotTest") {}
};

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

} // namespace

int main() {
    test_round_trip_restores_destroyed_child_hierarchy();
    test_forward_parent_reference_is_order_independent();
    test_sprite_outline_authoring_round_trips();
    test_all_project_scenes_match_the_registered_schema();
    std::cout << "Scene serializer tests passed\n";
    return 0;
}
