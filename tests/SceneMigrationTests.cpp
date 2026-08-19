#include "ecs/Components.hpp"
#include "scene/ComponentRegistry.hpp"
#include "scene/SceneSchema.hpp"

#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

namespace {

using namespace Engine;
using Json = nlohmann::json;

ComponentRegistry make_registry(std::uint32_t schema_version, int* migration_calls = nullptr) {
    ComponentRegistry registry;
    TypedComponentRegistration<TransformComponent> registration{
        .display_name = "Transform",
        .serialized_key = "transform",
        .schema_version = schema_version,
        .required = true,
        .serialize = [](const TransformComponent&, const ComponentSerializationContext&) {
            return Json::object();
        },
        .deserialize = [](
            TransformComponent&,
            const Json&,
            const ComponentSerializationContext&,
            std::string&
        ) { return true; }
    };
    if (migration_calls) {
        registration.migrate = [migration_calls](
            std::uint32_t from_version,
            Json& payload,
            std::string&
        ) {
            payload["migrated_from"] = from_version;
            ++*migration_calls;
            return true;
        };
    }
    std::string error;
    assert(registry.register_component(std::move(registration), &error));
    return registry;
}

Json scene(std::uint32_t version, const Json& component_versions) {
    return {
        {"scene_name", "MigrationTest"},
        {"version", version},
        {"component_versions", component_versions},
        {"entities", Json::array({
            Json{{"id", 1}, {"transform", Json::object()}},
            Json{{"id", 2}, {"transform", Json::object()}}
        })}
    };
}

void test_v1_scene_migrates_to_current_version() {
    int migration_calls = 0;
    ComponentRegistry registry = make_registry(3, &migration_calls);
    Json legacy = scene(1, Json::object());
    legacy.erase("component_versions");
    std::string error;

    assert(SceneSchema::migrate_to_current(legacy, registry, error));
    assert(SceneSchema::validate_structure(legacy, registry, error));
    assert(legacy.at("version") == SceneSchema::CURRENT_VERSION);
    assert(migration_calls == 4);
    assert(legacy.at("component_versions").at("transform") == 3);
}

void test_component_migration_updates_every_payload() {
    int migration_calls = 0;
    ComponentRegistry registry = make_registry(3, &migration_calls);
    Json value = scene(2, Json{{"transform", 1}});
    std::string error;

    assert(SceneSchema::migrate_to_current(value, registry, error));
    assert(SceneSchema::validate_structure(value, registry, error));
    assert(migration_calls == 4);
    for (const Json& entity : value.at("entities")) {
        assert(entity.at("transform").at("migrated_from") == 2);
    }
    assert(value.at("component_versions").at("transform") == 3);
}

void test_unsupported_versions_fail_with_actionable_errors() {
    ComponentRegistry registry = make_registry(2);
    std::string error;
    Json future_scene = scene(SceneSchema::CURRENT_VERSION + 1, Json{{"transform", 2}});
    assert(!SceneSchema::migrate_to_current(future_scene, registry, error));
    assert(error.find("unsupported future schema version") != std::string::npos);

    error.clear();
    Json missing_component_migration = scene(2, Json{{"transform", 1}});
    assert(!SceneSchema::migrate_to_current(missing_component_migration, registry, error));
    assert(error.find("No migration from schema version 1") != std::string::npos);
}

} // namespace

int main() {
    test_v1_scene_migrates_to_current_version();
    test_component_migration_updates_every_payload();
    test_unsupported_versions_fail_with_actionable_errors();
    std::cout << "Scene migration tests passed\n";
    return 0;
}
