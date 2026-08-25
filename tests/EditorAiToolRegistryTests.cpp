#include "editor/EditorSelection.hpp"
#include "editor/ai/EditorAiToolRegistry.hpp"
#include "editor/ai/EditorAiViewportTools.hpp"
#include "core/AssetDatabase.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "scene/Scene.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

using namespace Engine;

class TestScene final : public Scene {
public:
    TestScene() : Scene("AI Test Scene") {}
};

EditorAiToolRegistry make_registry() {
    return EditorAiToolRegistry({
        {
            "tag",
            [](const EntityManager& entities, EntityID entity) {
                return entities.has_component<TagComponent>(entity);
            },
            [](const EntityManager& entities, EntityID entity, const ComponentSerializationContext&) {
                return nlohmann::json{{"name", entities.get_component<TagComponent>(entity).tag}};
            }
        },
        {
            "transform",
            [](const EntityManager& entities, EntityID entity) {
                return entities.has_component<TransformComponent>(entity);
            },
            [](const EntityManager& entities, EntityID entity, const ComponentSerializationContext&) {
                const TransformComponent& transform = entities.get_component<TransformComponent>(entity);
                return nlohmann::json{
                    {"position", {transform.position.x, transform.position.y, transform.position.z}}
                };
            }
        }
    }, ENGINE_SOURCE_DIR);
}

void test_tool_schemas_are_read_only_and_strict() {
    const EditorAiToolRegistry registry = make_registry();
    const auto& schemas = registry.tool_schemas();

    assert(schemas.is_array());
    assert(schemas.size() == 10);
    assert(schemas.at(0).at("name") == "get_scene_summary");
    assert(schemas.at(0).at("strict") == true);
    assert(schemas.at(0).at("parameters").at("additionalProperties") == false);
    assert(schemas.at(1).at("name") == "get_selection_context");
    assert(schemas.at(2).at("name") == "find_scene_entities");
    assert(schemas.at(3).at("name") == "get_entity_details");
    assert(schemas.at(4).at("name") == "find_project_assets");
    assert(schemas.at(5).at("name") == "get_asset_details");
    assert(schemas.at(6).at("name") == "list_engine_documents");
    assert(schemas.at(7).at("name") == "read_engine_document");
    assert(schemas.at(8).at("name") == "get_build_status");
    assert(schemas.at(9).at("name") == "run_project_command");
}

void test_viewport_tool_schemas_and_validation() {
    const EditorAiViewportTools tools;
    const auto& schemas = tools.tool_schemas();
    assert(schemas.size() == 8);
    assert(schemas.at(0).at("name") == "get_viewport_state");
    assert(schemas.at(6).at("name") == "capture_viewport");
    assert(schemas.at(7).at("name") == "get_scene_diagnostics");
    for (const auto& schema : schemas) {
        assert(schema.at("strict") == true);
        assert(schema.at("parameters").at("additionalProperties") == false);
    }

    EditorAiViewportToolCommand command;
    std::string error;
    assert(tools.parse("pan_viewport", {{"delta_x", 24.0}, {"delta_y", -12.0}}, command, error));
    assert(command.type == EditorAiViewportToolType::Pan);
    assert(command.x == 24.0f && command.y == -12.0f);

    assert(tools.parse(
        "focus_viewport_entities",
        {{"entity_ids", nlohmann::json::array({3, 9})}, {"padding", 0.2}},
        command,
        error
    ));
    assert(command.type == EditorAiViewportToolType::FocusEntities);
    assert(command.entity_ids == std::vector<EntityID>({3, 9}));

    assert(tools.parse(
        "focus_viewport_entities",
        {{"entity_ids", nlohmann::json::array({3, 9})}},
        command,
        error
    ));
    assert(command.padding == 0.15f);
    assert(tools.parse("frame_all_viewport", nlohmann::json::object(), command, error));
    assert(command.type == EditorAiViewportToolType::FrameAll);
    assert(command.padding == 0.15f);
    assert(tools.parse("get_scene_diagnostics", nlohmann::json::object(), command, error));
    assert(command.type == EditorAiViewportToolType::GetDiagnostics);
    assert(command.limit == 50);

    assert(!tools.parse(
        "focus_viewport_entities",
        {{"entity_ids", nlohmann::json::array({3, 3})}, {"padding", 0.2}},
        command,
        error
    ));
    assert(error == "Viewport entity IDs must be unique");
    assert(!tools.parse("zoom_viewport", {{"factor", 8.0}}, command, error));
    assert(!tools.parse("capture_viewport", {{"unexpected", true}}, command, error));
}

void test_project_assets_are_frozen_and_queried_by_handle() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        "engine2d_ai_asset_registry_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "assets" / "textures");
    {
        std::ofstream file(root / "assets" / "textures" / "hero.png", std::ios::binary);
        file << "fake png";
    }

    AssetDatabase assets(root);
    const auto handle = assets.import_asset("assets/textures/hero.png");
    assert(handle.has_value());

    TestScene scene;
    ComponentSerializationContext serialization;
    serialization.asset_database = &assets;
    const EditorAiToolRegistry registry = make_registry();
    const nlohmann::json snapshot = registry.build_context_snapshot(&scene, nullptr, serialization);
    assert(snapshot.at("asset_index").size() == 1);

    const EditorAiToolResult found = registry.execute_snapshot(
        "find_project_assets",
        {{"query", "hero"}, {"asset_type", "texture"}, {"ready_only", true}, {"limit", 10}},
        snapshot
    );
    assert(found.success);
    assert(found.output.at("total_matches") == 1);
    assert(found.output.at("matches").at(0).at("asset_handle") == handle->to_string());
    assert(!found.output.at("matches").at(0).contains("import_settings"));

    const EditorAiToolResult details = registry.execute_snapshot(
        "get_asset_details", {{"asset_handle", handle->to_string()}}, snapshot
    );
    assert(details.success);
    assert(details.output.at("path") == "assets/textures/hero.png");
    assert(details.output.at("type") == "texture");
    assert(details.output.contains("import_settings"));

    std::filesystem::remove_all(root);
}

void test_engine_documents_are_allowlisted() {
    const EditorAiToolRegistry registry = make_registry();
    const EditorAiToolResult listed = registry.execute(
        "list_engine_documents", nlohmann::json::object(), nullptr, nullptr
    );
    assert(listed.success);
    assert(listed.output.at("documents").size() == 7);

    const EditorAiToolResult renderer = registry.execute(
        "read_engine_document", nlohmann::json{{"document_id", "render_materials"}}, nullptr, nullptr
    );
    assert(renderer.success);
    assert(renderer.output.at("content").get<std::string>().find("# Render materials") != std::string::npos);

    const EditorAiToolResult authoring = registry.execute(
        "read_engine_document", nlohmann::json{{"document_id", "ai_authoring"}}, nullptr, nullptr
    );
    assert(authoring.success);
    assert(authoring.output.at("content").get<std::string>().find("# AI-assisted authoring contract") != std::string::npos);

    const EditorAiToolResult disallowed = registry.execute(
        "read_engine_document", nlohmann::json{{"document_id", "../../.ssh/id_rsa"}}, nullptr, nullptr
    );
    assert(!disallowed.success);
}

void test_scene_summary_reports_active_selection() {
    TestScene scene;
    EntityManager& entities = scene.get_entity_manager();
    const EntityID root = entities.create_entity();
    const EntityID child = entities.create_entity();
    entities.add_component<TransformComponent>(root, TransformComponent{});
    entities.add_component<TransformComponent>(child, TransformComponent{});
    assert(entities.set_parent(child, root).status == HierarchyOperationStatus::Changed);

    EditorSelection selection;
    selection.select(child);
    selection.select(9999, true);

    const EditorAiToolResult result = make_registry().execute(
        "get_scene_summary", nlohmann::json::object(), &scene, &selection
    );
    assert(result.success);
    assert(result.output.at("name") == "AI Test Scene");
    assert(result.output.at("entity_count") == 2);
    assert(result.output.at("root_entity_count") == 1);
    assert(result.output.at("selected_entity_ids") == nlohmann::json::array({child}));
}

void test_selection_context_is_deterministic_and_component_aware() {
    TestScene scene;
    EntityManager& entities = scene.get_entity_manager();
    const EntityID entity = entities.create_entity();
    entities.add_component<TagComponent>(entity, TagComponent{"Player"});
    entities.add_component<TransformComponent>(entity, TransformComponent{});

    EditorSelection selection;
    selection.select(entity);

    const EditorAiToolResult result = make_registry().execute(
        "get_selection_context", nlohmann::json::object(), &scene, &selection
    );
    assert(result.success);
    assert(result.output.at("count") == 1);
    const auto& selected = result.output.at("entities").at(0);
    assert(selected.at("id") == entity);
    assert(selected.at("name") == "Player");
    assert(selected.at("components") == nlohmann::json::array({"tag", "transform"}));
}

void test_existing_entities_can_be_found_and_inspected_by_stable_id() {
    TestScene scene;
    EntityManager& entities = scene.get_entity_manager();
    const EntityID enemy = entities.create_entity();
    entities.add_component<TagComponent>(enemy, TagComponent{"Elite Enemy"});
    TransformComponent transform;
    transform.position = {12.0f, 34.0f, 5.0f};
    entities.add_component<TransformComponent>(enemy, transform);
    const EntityID platform = entities.create_entity();
    entities.add_component<TagComponent>(platform, TagComponent{"Enemy Platform"});
    entities.add_component<TransformComponent>(platform, TransformComponent{});

    EditorSelection selection;
    selection.select(enemy);
    const EditorAiToolRegistry registry = make_registry();
    const nlohmann::json snapshot = registry.build_context_snapshot(&scene, &selection);
    const EditorAiToolResult found = registry.execute_snapshot(
        "find_scene_entities",
        {{"query", "elite"}, {"component_key", "transform"}, {"limit", 10}},
        snapshot
    );
    assert(found.success);
    assert(found.output.at("total_matches") == 1);
    assert(found.output.at("exact_name_matches") == 0);
    assert(found.output.at("unambiguous") == true);
    assert(found.output.at("matches").at(0).at("entity_id") == enemy);
    assert(found.output.at("matches").at(0).at("selected") == true);

    const EditorAiToolResult details = registry.execute_snapshot(
        "get_entity_details", {{"entity_id", enemy}}, snapshot
    );
    assert(details.success);
    assert(details.output.at("name") == "Elite Enemy");
    assert(details.output.at("transform").at("position") == nlohmann::json::array({12.0, 34.0, 5.0}));
    assert(details.output.at("component_data").at("transform").at("position") ==
        nlohmann::json::array({12.0, 34.0, 5.0}));

    const EditorAiToolResult missing = registry.execute_snapshot(
        "get_entity_details", {{"entity_id", 999999}}, snapshot
    );
    assert(!missing.success);
}

void test_invalid_calls_fail_without_touching_scene() {
    TestScene scene;
    const std::size_t before = scene.get_entity_manager().get_living_entity_count();
    const EditorAiToolRegistry registry = make_registry();

    const EditorAiToolResult unknown = registry.execute(
        "delete_everything", nlohmann::json::object(), &scene, nullptr
    );
    assert(!unknown.success);
    assert(scene.get_entity_manager().get_living_entity_count() == before);

    const EditorAiToolResult invalid_arguments = registry.execute(
        "get_scene_summary", nlohmann::json{{"unexpected", true}}, &scene, nullptr
    );
    assert(!invalid_arguments.success);
    assert(scene.get_entity_manager().get_living_entity_count() == before);
}

void test_context_snapshot_handles_missing_scene() {
    const nlohmann::json snapshot = make_registry().build_context_snapshot(nullptr, nullptr);
    assert(snapshot.at("schema_version") == 1);
    assert(snapshot.at("access") == "read_only");
    assert(snapshot.at("scene").contains("error"));
    assert(snapshot.at("selection").contains("error"));
}

} // namespace

int main() {
    test_tool_schemas_are_read_only_and_strict();
    test_viewport_tool_schemas_and_validation();
    test_scene_summary_reports_active_selection();
    test_selection_context_is_deterministic_and_component_aware();
    test_existing_entities_can_be_found_and_inspected_by_stable_id();
    test_project_assets_are_frozen_and_queried_by_handle();
    test_invalid_calls_fail_without_touching_scene();
    test_context_snapshot_handles_missing_scene();
    test_engine_documents_are_allowlisted();
    std::cout << "Editor AI tool registry tests passed\n";
    return 0;
}
