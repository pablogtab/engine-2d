#include "editor/EditorSelection.hpp"
#include "editor/ai/EditorAiToolRegistry.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "scene/Scene.hpp"

#include <cassert>
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
            }
        },
        {
            "transform",
            [](const EntityManager& entities, EntityID entity) {
                return entities.has_component<TransformComponent>(entity);
            }
        }
    }, ENGINE_SOURCE_DIR);
}

void test_tool_schemas_are_read_only_and_strict() {
    const EditorAiToolRegistry registry = make_registry();
    const auto& schemas = registry.tool_schemas();

    assert(schemas.is_array());
    assert(schemas.size() == 4);
    assert(schemas.at(0).at("name") == "get_scene_summary");
    assert(schemas.at(0).at("strict") == true);
    assert(schemas.at(0).at("parameters").at("additionalProperties") == false);
    assert(schemas.at(1).at("name") == "get_selection_context");
    assert(schemas.at(2).at("name") == "list_engine_documents");
    assert(schemas.at(3).at("name") == "read_engine_document");
}

void test_engine_documents_are_allowlisted() {
    const EditorAiToolRegistry registry = make_registry();
    const EditorAiToolResult listed = registry.execute(
        "list_engine_documents", nlohmann::json::object(), nullptr, nullptr
    );
    assert(listed.success);
    assert(listed.output.at("documents").size() == 6);

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
    test_scene_summary_reports_active_selection();
    test_selection_context_is_deterministic_and_component_aware();
    test_invalid_calls_fail_without_touching_scene();
    test_context_snapshot_handles_missing_scene();
    test_engine_documents_are_allowlisted();
    std::cout << "Editor AI tool registry tests passed\n";
    return 0;
}
