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

    std::cout << "ScriptSerializationTests passed\n";
    return 0;
}
