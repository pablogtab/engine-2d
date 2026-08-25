#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
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

    TypedComponentRegistration<CameraComponent> cam_reg{
        .display_name = "Camera 2D",
        .serialized_key = "camera",
        .schema_version = schema_version,
        .required = false,
        .serialize = [](const CameraComponent&, const ComponentSerializationContext&) {
            return Json::object();
        },
        .deserialize = [](
            CameraComponent&,
            const Json&,
            const ComponentSerializationContext&,
            std::string&
        ) { return true; }
    };
    assert(registry.register_component(std::move(cam_reg), &error));
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

void test_v2_or_v3_scene_with_camera_migrates_post_process_to_camera_component() {
    ComponentRegistry registry = make_registry(1);
    Json v2_doc = scene(2, Json{{"transform", 1}, {"camera", 1}});
    v2_doc["entities"].push_back(Json{
        {"id", 10},
        {"transform", Json::object()},
        {"camera", Json{{"is_primary", true}}}
    });
    v2_doc["post_process_profile_handle"] = "42";
    v2_doc["render_layer_effect_assignments"] = Json::array({
        Json{{"profile_handle", "100"}, {"render_layer", 2}, {"enabled", true}}
    });
    v2_doc["custom_metadata"] = Json{{"author", "TestUser"}};

    std::string error;
    assert(SceneSchema::migrate_to_current(v2_doc, registry, error));
    assert(SceneSchema::validate_structure(v2_doc, registry, error));
    assert(v2_doc.at("version") == SceneSchema::CURRENT_VERSION);
    assert(!v2_doc.contains("post_process_profile_handle"));
    assert(!v2_doc.contains("render_layer_effect_assignments"));
    assert(v2_doc.contains("settings"));
    assert(!v2_doc.at("settings").contains("post_process_profile_handle"));
    assert(!v2_doc.at("settings").contains("render_layer_effect_assignments"));
    assert(v2_doc.at("custom_metadata").at("author") == "TestUser");

    // Camera entity has the migrated post_process block
    const auto& entities = v2_doc.at("entities");
    const auto it = std::find_if(entities.begin(), entities.end(), [](const Json& e) {
        return e.contains("camera");
    });
    assert(it != entities.end());
    const auto& cam = it->at("camera");
    assert(cam.contains("post_process"));
    assert(cam.at("post_process").at("enabled") == true);
    assert(cam.at("post_process").at("global_profile_handle") == "42");
    assert(cam.at("post_process").at("layer_effects").size() == 1);
    assert(cam.at("post_process").at("layer_effects")[0].at("effect_handle") == "100");
    assert(cam.at("post_process").at("layer_effects")[0].at("render_layer") == 2);
}

void test_v3_scene_without_camera_fails_migration_safely() {
    ComponentRegistry registry = make_registry(1);
    Json v3_doc = scene(3, Json{{"transform", 1}});
    v3_doc["settings"] = Json{
        {"physics_enabled", true},
        {"gravity", {0.0f, -980.0f}},
        {"post_process_profile_handle", "42"}
    };
    const Json original = v3_doc;

    std::string error;
    assert(!SceneSchema::migrate_to_current(v3_doc, registry, error));
    assert(error.find("Cannot migrate post-processing to CameraComponent: scene has no camera entity.") != std::string::npos);
    assert(v3_doc == original);
}

void test_v3_scene_without_post_process_migrates_to_v4() {
    ComponentRegistry registry = make_registry(1);
    Json v3_doc = scene(3, Json{{"transform", 1}});
    v3_doc["settings"] = Json{
        {"physics_enabled", true},
        {"gravity", {0.0f, -980.0f}}
    };

    std::string error;
    assert(SceneSchema::migrate_to_current(v3_doc, registry, error));
    assert(SceneSchema::validate_structure(v3_doc, registry, error));
    assert(v3_doc.at("version") == SceneSchema::CURRENT_VERSION);
    assert(v3_doc.at("settings").at("physics_enabled") == true);
}

} // namespace

int main() {
    test_v1_scene_migrates_to_current_version();
    test_v2_or_v3_scene_with_camera_migrates_post_process_to_camera_component();
    test_v3_scene_without_camera_fails_migration_safely();
    test_v3_scene_without_post_process_migrates_to_v4();
    test_component_migration_updates_every_payload();
    test_unsupported_versions_fail_with_actionable_errors();
    std::cout << "Scene migration tests passed\n";
    return 0;
}
