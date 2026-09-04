#include "editor/EditorSelection.hpp"
#include "editor/ai/EditorAiToolRegistry.hpp"
#include "editor/ai/EditorAiViewportTools.hpp"
#include "core/AssetDatabase.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "scene/Scene.hpp"
#include "enginescript/core/LanguageVersion.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

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
        },
       {"gameplay_tags",
        [](const EntityManager &entities, EntityID entity) {
          return entities.has_component<GameplayTagsComponent>(entity);
        },
        [](const EntityManager &entities, EntityID entity,
           const ComponentSerializationContext &) {
          return nlohmann::json{
              {"tags",
               entities.get_component<GameplayTagsComponent>(entity).tags}};
        }}}, ENGINE_SOURCE_DIR);
}

void test_tool_schemas_are_read_only_and_strict() {
    const EditorAiToolRegistry registry = make_registry();
    const auto& schemas = registry.tool_schemas();

    assert(schemas.is_array());
    assert(schemas.size() == 16);
    assert(schemas.at(0).at("name") == "get_scene_summary");
    assert(schemas.at(0).at("strict") == true);
    assert(schemas.at(0).at("parameters").at("additionalProperties") == false);
    assert(schemas.at(1).at("name") == "get_selection_context");
    assert(schemas.at(2).at("name") == "get_project_overview");
    assert(schemas.at(3).at("name") == "find_entities_by_gameplay_tag");
    assert(schemas.at(4).at("name") == "find_scene_entities");
    assert(schemas.at(5).at("name") == "get_entity_details");
    assert(schemas.at(6).at("name") == "find_project_assets");
    assert(schemas.at(7).at("name") == "get_asset_details");
    assert(schemas.at(6).at("description").get<std::string>().find(
        "ready match is sufficient"
    ) != std::string::npos);
    assert(schemas.at(7).at("description").get<std::string>().find(
        "do not call it merely"
    ) != std::string::npos);
    assert(schemas.at(8).at("name") == "list_engine_documents");
  assert(schemas.at(9).at("name") == "read_engine_document");
  assert(schemas.at(10).at("name") == "list_project_documents");
  assert(schemas.at(11).at("name") == "read_project_document");
  assert(schemas.at(12).at("name") == "search_project_documents");
  assert(schemas.at(13).at("name") == "get_engine_capabilities");
  assert(schemas.at(14).at("name") == "get_build_status");
    assert(schemas.at(15).at("name") == "run_project_command");
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

    const EditorAiToolResult related = registry.execute_snapshot(
        "find_project_assets",
        {{"query", "hero"}, {"asset_type", "prefab"}, {"ready_only", true}, {"limit", 10}},
        snapshot
    );
    assert(related.success);
    assert(related.output.at("total_matches") == 0);
    assert(related.output.at("related_matches").size() == 1);
    assert(related.output.at("related_matches").at(0).at("asset_handle") ==
        handle->to_string());
    assert(related.output.contains("search_guidance"));

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
    assert(listed.output.at("documents").size() == 10);

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

void test_gameplay_tags_drive_semantic_discovery() {
  TestScene scene;
  EntityManager &entities = scene.get_entity_manager();
  const EntityID player = entities.create_entity();
  entities.add_component<TagComponent>(player, TagComponent{"Player Knight"});
  entities.add_component<GameplayTagsComponent>(
      player, GameplayTagsComponent{{"character.controllable", "player"}});
  const EntityID camera = entities.create_entity();
  entities.add_component<TagComponent>(camera, TagComponent{"Main Camera"});
  entities.add_component<GameplayTagsComponent>(
      camera, GameplayTagsComponent{{"camera.main"}});

  const EditorAiToolRegistry registry = make_registry();
  const nlohmann::json snapshot =
      registry.build_context_snapshot(&scene, nullptr);
  const EditorAiToolResult found =
      registry.execute_snapshot("find_entities_by_gameplay_tag",
                                {{"tag", "player"}, {"limit", 10}}, snapshot);
  assert(found.success);
  assert(found.output.at("total_matches") == 1);
  assert(found.output.at("matches").at(0).at("entity_id") == player);
  assert(found.output.at("matches").at(0).at("gameplay_tags") ==
         nlohmann::json::array({"character.controllable", "player"}));

  const EditorAiToolResult overview = registry.execute_snapshot(
      "get_project_overview", nlohmann::json::object(), snapshot);
  assert(overview.success);
  assert(
      overview.output.at("gameplay_roles").at("player").at(0).at("entity_id") ==
      player);
  assert(overview.output.at("gameplay_roles")
             .at("camera.main")
             .at(0)
             .at("entity_id") == camera);

  const EditorAiToolResult invalid =
      registry.execute_snapshot("find_entities_by_gameplay_tag",
                                {{"tag", "Player"}, {"limit", 10}}, snapshot);
  assert(!invalid.success);
}

void test_project_documents_are_bounded_and_cannot_traverse() {
  const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                     "engine2d_ai_project_knowledge_test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "Docs");
  {
    std::ofstream file(root / "AGENTS.md");
    file << "# Crystal Cavern\nThe player collects coins.\n";
  }
    {
        std::ofstream file(root / "Docs" / "GAMEPLAY.md");
        file << "# Gameplay\nA moving platform crosses the cavern.\n";
    }
    const std::filesystem::path outside = root.parent_path() /
        "engine2d_ai_project_knowledge_secret.md";
    {
        std::ofstream file(outside);
        file << "This must not be exposed.";
    }
    std::error_code symlink_error;
    std::filesystem::create_symlink(outside, root / "README.md", symlink_error);
    assert(!symlink_error);
  const std::filesystem::path previous_root =
      AssetDatabase::get().project_root();
  AssetDatabase::get().set_project_root(root);

  const EditorAiToolRegistry registry = make_registry();
  const EditorAiToolResult listed = registry.execute(
      "list_project_documents", nlohmann::json::object(), nullptr, nullptr);
  assert(listed.success);
  std::set<std::string> document_ids;
  std::set<std::string> normalized_document_ids;
  for (const nlohmann::json &document : listed.output.at("documents")) {
    const std::string document_id = document.at("document_id").get<std::string>();
    document_ids.insert(document_id);
    std::string normalized = document_id;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char character) {
                     return static_cast<char>(std::tolower(character));
                   });
    normalized_document_ids.insert(std::move(normalized));
  }
  assert(document_ids.size() == listed.output.at("documents").size());
  assert(normalized_document_ids.size() == listed.output.at("documents").size());
    assert(document_ids.contains("AGENTS.md"));
    assert(document_ids.contains("Docs/GAMEPLAY.md"));
    assert(!document_ids.contains("README.md"));

  const EditorAiToolResult read =
      registry.execute("read_project_document",
                       {{"document_id", "Docs/GAMEPLAY.md"}}, nullptr, nullptr);
  assert(read.success);
  assert(read.output.at("content").get<std::string>().find("moving platform") !=
         std::string::npos);

  const EditorAiToolResult searched = registry.execute(
      "search_project_documents", {{"query", "collects coins"}, {"limit", 5}},
      nullptr, nullptr);
  assert(searched.success);
  assert(searched.output.at("matches").size() == 1);

  const EditorAiToolResult traversal =
      registry.execute("read_project_document",
                       {{"document_id", "../CLAUDE.md"}}, nullptr, nullptr);
  assert(!traversal.success);
  const EditorAiToolResult bad_limit =
      registry.execute("search_project_documents",
                       {{"query", "coins"}, {"limit", 21}}, nullptr, nullptr);
  assert(!bad_limit.success);

  const nlohmann::json snapshot =
      registry.build_context_snapshot(nullptr, nullptr);
  const auto &previews =
      snapshot.at("project_overview").at("project_document_previews");
  assert(std::any_of(
      previews.begin(), previews.end(), [](const nlohmann::json &preview) {
        return preview.value("preview", "").find("player collects coins") !=
               std::string::npos;
      }));

    AssetDatabase::get().set_project_root(previous_root);
    std::filesystem::remove_all(root);
    std::filesystem::remove(outside);
}

void test_engine_capabilities_report_the_compiled_contract() {
  const EditorAiToolResult result = make_registry().execute(
      "get_engine_capabilities", nlohmann::json::object(), nullptr, nullptr);
  assert(result.success);
  assert(result.output.at("engine_script").at("abi_version") ==
         EngineScript::ENGINE_SCRIPT_ABI_VERSION);
  assert(result.output.at("engine_script").at("sdk_hash") ==
         EngineScript::ENGINE_SCRIPT_SDK_HASH);
  assert(result.output.at("engine_script").at("language_version") ==
         EngineScript::ENGINE_SCRIPT_LANGUAGE_VERSION);
  assert(result.output.at("engine_script").at("compiler_cache_id") ==
         EngineScript::ENGINE_SCRIPT_COMPILER_CACHE_ID);
  assert(result.output.at("engine_script").at("features").at("exhaustive_match") == true);
  assert(result.output.at("engine_script").at("features").at("deque") == true);
  const auto &components = result.output.at("registered_components");
  assert(std::any_of(components.begin(), components.end(),
                     [](const nlohmann::json &component) {
                       return component.value("key", "") == "gameplay_tags";
                     }));
  assert(result.output.at("runtime").at("runtime_editing_ephemeral") == true);
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
  test_gameplay_tags_drive_semantic_discovery();
  test_project_assets_are_frozen_and_queried_by_handle();
  test_project_documents_are_bounded_and_cannot_traverse();
  test_engine_capabilities_report_the_compiled_contract();
    test_invalid_calls_fail_without_touching_scene();
    test_context_snapshot_handles_missing_scene();
    test_engine_documents_are_allowlisted();
    std::cout << "Editor AI tool registry tests passed\n";
    return 0;
}
