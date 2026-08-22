#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/ScriptingComponents.hpp"
#include "core/FileSystem.hpp"
#include "scene/Scene.hpp"
#include "scene/ComponentRegistry.hpp"
#include "scene/SceneSerializer.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {

class TestScene final : public Engine::Scene {
public:
    TestScene() : Scene("ScriptSerialization") {}
};

} // namespace

int main() {
    using namespace Engine;
    TestScene scene;
    EntityManager& entities = scene.get_entity_manager();
    const EntityID target = entities.create_entity();
    entities.add_component<TransformComponent>(target, TransformComponent{});
    const EntityID scripted = entities.create_entity();
    entities.add_component<TransformComponent>(scripted, TransformComponent{});

    ScriptComponent expected;
    expected.script_handle = AssetHandle{0x1234};
    expected.enabled = false;
    expected.public_fields = {
        {"active", ScriptFieldType::Boolean, true},
        {"speed", ScriptFieldType::Number, 12.5},
        {"title", ScriptFieldType::String, std::string("Hero")},
        {"offset", ScriptFieldType::Vector2, ScriptVector2{3.0f, 4.0f}},
        {"target", ScriptFieldType::Entity, target},
        {"texture", ScriptFieldType::Asset, AssetHandle{0x5678}}
    };
    entities.add_component<ScriptComponent>(scripted, expected);

    SceneSerializer serializer(&scene);
    const std::string snapshot = serializer.serialize_to_string();
    assert(!snapshot.empty());
    entities.get_component<ScriptComponent>(scripted).public_fields.clear();
    assert(serializer.deserialize_from_string(snapshot));
    assert(entities.get_component<ScriptComponent>(scripted) == expected);

    default_component_registry().remap_entity_references(
        entities, scripted, {{target, EntityID{77}}}
    );
    const auto& remapped = entities.get_component<ScriptComponent>(scripted);
    assert(std::get<EntityID>(remapped.public_fields[4].value) == 77);

    const auto platformer_document = nlohmann::json::parse(FileSystem::read_text(
        std::string(ENGINE_SOURCE_DIR) + "/assets/scenes/PlatformerDemo.scene"
    ));
    const auto player_json = std::find_if(
        platformer_document.at("entities").begin(),
        platformer_document.at("entities").end(),
        [](const nlohmann::json& value) { return value.value("tag", "") == "Player_Knight"; }
    );
    assert(player_json != platformer_document.at("entities").end());
    EntityManager platformer_entities;
    const EntityID player = platformer_entities.create_entity();
    std::string platformer_error;
    assert(default_component_registry().deserialize_entity(
        platformer_entities, player, *player_json, {}, platformer_error
    ));
    assert(platformer_entities.has_component<ScriptComponent>(player));
    const auto& controller = platformer_entities.get_component<ScriptComponent>(player);
    assert(controller.script_handle == AssetHandle{0x8f5c06f05bd74531});
    assert(controller.public_fields.size() == 4);

    std::cout << "ScriptSerializationTests passed\n";
    return 0;
}
