#include "core/AssetDatabase.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/ScriptingComponents.hpp"
#include "scene/ComponentRegistry.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneSerializer.hpp"
#include "scripting/ScriptRuntime.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace {

class TestScene final : public Engine::Scene {
public:
    explicit TestScene(std::string name = "ScriptSerialization") : Scene(std::move(name)) {}
};

void write_file(const std::filesystem::path& path, const std::string& source) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << source;
}

const Engine::ScriptPublicField* find_field(
    const std::vector<Engine::ScriptPublicField>& fields,
    std::string_view name
) {
    const auto found = std::find_if(
        fields.begin(),
        fields.end(),
        [name](const Engine::ScriptPublicField& field) { return field.name == name; }
    );
    return found == fields.end() ? nullptr : &*found;
}

void test_round_trip_references_and_transactional_rejection() {
    using namespace Engine;

    TestScene scene("RoundTrip");
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
        {"texture", ScriptFieldType::Asset, AssetHandle{0x5678}},
        {"optional_texture", ScriptFieldType::Asset, AssetHandle{}}
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

    assert(serializer.deserialize_from_string(snapshot));
    nlohmann::json invalid = nlohmann::json::parse(snapshot);
    for (auto& entity : invalid["entities"]) {
        if (entity.value("id", EntityID{0}) != scripted) continue;
        entity["script"]["public_fields"][1]["type"] = "boolean";
    }
    assert(!serializer.deserialize_from_string(invalid.dump()));
    assert(entities.get_component<ScriptComponent>(scripted) == expected);
}

void test_defaults_type_changes_and_schema_evolution(
    const std::filesystem::path& root,
    Engine::AssetDatabase& database
) {
    using namespace Engine;

    TestScene scene("ScriptSchemaEvolution");
    EntityManager& entities = scene.get_entity_manager();
    const EntityID target = entities.create_entity();
    entities.add_component<TransformComponent>(target, TransformComponent{});
    const EntityID scripted = entities.create_entity();
    entities.add_component<TransformComponent>(scripted, TransformComponent{});

    const std::string source = R"(
@behaviour
export class Evolution extends Behaviour {
    @expose
    speed: String = "walk";

    @expose
    lives: Float = 3.0;

    @expose
    new_enabled: Bool = true;
}
)";
    write_file(root / "Assets/scripts/Evolution.e2s", source);
    const AssetScanResult scan = database.scan("Assets");
    assert(scan.ready == 1);
    const AssetRecord* script_asset = database.find_by_path(root / "Assets/scripts/Evolution.e2s");
    assert(script_asset && script_asset->type == AssetType::Script);

    const ScriptInspectionResult inspection =
        ScriptRuntime::inspect_asset(script_asset->handle, database);
    assert(inspection.success);
    assert(inspection.public_fields.size() == 3);
    assert(std::get<std::string>(find_field(inspection.public_fields, "speed")->value) == "walk");
    assert(std::get<double>(find_field(inspection.public_fields, "lives")->value) == 3.0);
    assert(std::get<bool>(find_field(inspection.public_fields, "new_enabled")->value));

    ScriptComponent evolved;
    evolved.script_handle = script_asset->handle;
    evolved.public_fields = {
        {"speed", ScriptFieldType::String, std::string("walk")},
        {"lives", ScriptFieldType::Number, 7.0},
        {"new_enabled", ScriptFieldType::Boolean, true}
    };
    entities.add_component<ScriptComponent>(scripted, evolved);

    SceneSerializer serializer(&scene);
    const std::string snapshot = serializer.serialize_to_string();
    assert(!snapshot.empty());
    assert(serializer.deserialize_from_string(snapshot));
    assert(entities.get_component<ScriptComponent>(scripted) == evolved);

    ScriptRuntime runtime;
    const ScriptRuntimeStartResult started = runtime.start(entities, database);
    assert(started.started == 1 && started.failed == 0);
    runtime.shutdown();
}

void test_missing_script_is_preserved_and_fails_safely(Engine::AssetDatabase& database) {
    using namespace Engine;

    TestScene scene("MissingScript");
    EntityManager& entities = scene.get_entity_manager();
    const EntityID entity = entities.create_entity();
    entities.add_component<TransformComponent>(entity, TransformComponent{});
    ScriptComponent missing;
    missing.script_handle = AssetHandle{0xDEADBEEF};
    missing.public_fields = {{"speed", ScriptFieldType::Number, 5.0}};
    entities.add_component<ScriptComponent>(entity, missing);

    SceneSerializer serializer(&scene);
    const std::string snapshot = serializer.serialize_to_string();
    assert(!snapshot.empty());
    assert(serializer.deserialize_from_string(snapshot));
    assert(entities.get_component<ScriptComponent>(entity) == missing);

    ScriptRuntime runtime;
    const ScriptRuntimeStartResult started = runtime.start(entities, database);
    assert(started.started == 0 && started.failed == 1);
    const ScriptExecutionStatus* status = runtime.status(entity);
    assert(status && status->state == ScriptExecutionState::Error);
    assert(status->message.find("missing or unavailable") != std::string::npos);
    runtime.shutdown();
}

} // namespace

int main() {
    namespace fs = std::filesystem;

    test_round_trip_references_and_transactional_rejection();

    const fs::path root = fs::temp_directory_path() / "engine2d_script_serialization_tests";
    std::error_code error;
    fs::remove_all(root, error);
    Engine::AssetDatabase database(root);
    test_defaults_type_changes_and_schema_evolution(root, database);
    test_missing_script_is_preserved_and_fails_safely(database);
    fs::remove_all(root, error);

    std::cout << "ScriptSerializationTests passed\n";
    return 0;
}
