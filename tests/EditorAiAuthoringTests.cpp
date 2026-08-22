#include "editor/ai/EditorAiAuthoring.hpp"
#include "core/AssetDatabase.hpp"
#include "core/AssetManager.hpp"
#include "core/FileSystem.hpp"
#include "editor/EditorSelection.hpp"
#include "editor/commands/EditorCommandHistory.hpp"
#include "editor/state/EditorEntityStateStore.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/PhysicsComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>

namespace {

using namespace Engine;

struct TestSession {
    EntityManager entities;
    std::shared_ptr<EditorSelection> selection{std::make_shared<EditorSelection>()};
    std::shared_ptr<EditorEntityStateStore> state_store{std::make_shared<EditorEntityStateStore>()};
    EditorCommandHistory history;

    EditorAiAuthoringContext context() {
        return {
            .entities = &entities,
            .selection = selection,
            .entity_state_store = state_store,
            .component_registry = &default_component_registry()
        };
    }
};

EntityID create_basic_entity(TestSession& session, const std::string& name) {
    const EntityID entity = session.entities.create_entity();
    session.entities.add_component<TagComponent>(entity, TagComponent{name});
    session.entities.add_component<TransformComponent>(entity, TransformComponent{});
    session.entities.add_component<HierarchyComponent>(entity, HierarchyComponent{});
    return entity;
}

void queue(
    const EditorAiAuthoringRegistry& registry,
    std::vector<EditorAiAuthoringAction>& actions,
    const std::string& tool,
    nlohmann::json arguments
) {
    const EditorAiToolResult result = registry.queue_action(tool, arguments, actions);
    assert(result.success);
    assert(result.output.at("queued") == true);
}

void test_tool_validation_and_limits() {
    EditorAiAuthoringRegistry registry;
    std::vector<EditorAiAuthoringAction> actions;

    for (const auto& schema : registry.tool_schemas()) {
        const auto& parameters = schema.at("parameters");
        assert(schema.at("strict") == true);
        assert(parameters.at("additionalProperties") == false);
        assert(!parameters.contains("oneOf"));
        assert(parameters.at("required").size() == parameters.at("properties").size());
        for (auto property = parameters.at("properties").begin();
             property != parameters.at("properties").end(); ++property) {
            assert(std::find(
                parameters.at("required").begin(),
                parameters.at("required").end(),
                property.key()
            ) != parameters.at("required").end());
        }
        const std::string encoded = parameters.dump();
        assert(encoded.find("minLength") == std::string::npos);
        assert(encoded.find("maxLength") == std::string::npos);
        assert(encoded.find("uniqueItems") == std::string::npos);
    }

    assert(!registry.queue_action("unknown", nlohmann::json::object(), actions).success);
    assert(!registry.queue_action(
        "propose_create_entity", {{"alias", "bad alias"}, {"name", "Enemy"}}, actions
    ).success);
    assert(!registry.queue_action(
        "propose_create_entity",
        {{"alias", "enemy"}, {"name", "Enemy"}, {"scale", {0.0, 1.0}}},
        actions
    ).success);

    queue(registry, actions, "propose_create_entity", {{"alias", "enemy"}, {"name", "Enemy"}});
    assert(!registry.queue_action(
        "propose_create_entity", {{"alias", "enemy"}, {"name", "Duplicate"}}, actions
    ).success);
    assert(!registry.queue_action(
        "propose_add_component", {{"entity_alias", "enemy"}, {"component_key", "prefab_link"}}, actions
    ).success);
    assert(!registry.queue_action(
        "propose_configure_component",
        {{"entity_alias", "enemy"}, {"component_key", "sprite"}, {"patch_json", "not json"}},
        actions
    ).success);

    std::vector<EditorAiAuthoringAction> strict_arguments;
    assert(registry.queue_action("propose_create_entity", {
        {"alias", "strict_enemy"}, {"name", "Strict Enemy"},
        {"position", nullptr}, {"scale", nullptr}, {"pivot", nullptr},
        {"rotation", nullptr}, {"visible", nullptr}, {"parent_entity_id", nullptr},
        {"parent_entity_alias", nullptr}, {"components", nullptr}
    }, strict_arguments).success);
    assert(strict_arguments.at(0).arguments.size() == 2);

    while (actions.size() < EditorAiAuthoringRegistry::MAX_ACTIONS_PER_PROPOSAL) {
        queue(registry, actions, "propose_rename_entity", {
            {"entity_alias", "enemy"},
            {"name", "Enemy " + std::to_string(actions.size())}
        });
    }
    assert(!registry.queue_action(
        "propose_rename_entity", {{"entity_alias", "enemy"}, {"name", "Too Many"}}, actions
    ).success);

    const nlohmann::json frozen_context{
        {"entity_index", nlohmann::json::array({{
            {"entity_id", 42},
            {"components", nlohmann::json::array({"tag", "transform"})}
        }})}
    };
    std::vector<EditorAiAuthoringAction> frozen_actions;
    const EditorAiToolResult missing_id = registry.queue_action(
        "propose_delete_entity", {{"entity_id", 99}}, frozen_actions, &frozen_context
    );
    assert(!missing_id.success);
    assert(missing_id.error.find("find_scene_entities") != std::string::npos);
    const EditorAiToolResult missing_component = registry.queue_action(
        "propose_configure_component",
        {{"entity_id", 42}, {"component_key", "sprite"}, {"patch_json", R"({"size":[8,8]})"}},
        frozen_actions,
        &frozen_context
    );
    assert(!missing_component.success);
    assert(missing_component.error.find("does not contain component") != std::string::npos);
}

void test_alias_session_executes_as_one_undoable_transaction() {
    TestSession session;
    EditorAiAuthoringRegistry registry;
    std::vector<EditorAiAuthoringAction> actions;
    queue(registry, actions, "propose_create_entity", {
        {"alias", "enemy"}, {"name", "Enemy"},
        {"position", {2.0, 3.0, 4.0}}, {"components", {"sprite"}}
    });
    queue(registry, actions, "propose_set_transform", {
        {"entity_alias", "enemy"}, {"scale", {2.0, 3.0}}, {"rotation", 45.0}
    });
    queue(registry, actions, "propose_rename_entity", {
        {"entity_alias", "enemy"}, {"name", "Elite Enemy"}
    });
    queue(registry, actions, "propose_add_component", {
        {"entity_alias", "enemy"}, {"component_key", "collider"}
    });

    std::string error;
    auto group = registry.build_command_group(actions, session.context(), error);
    assert(group);
    assert(error.empty());
    assert(session.history.execute_command(std::move(group)));
    assert(session.history.get_undo_count() == 1);
    assert(session.history.get_undo_description() == "AI Session (4 actions)");

    const EntityID created = session.entities.find_by_tag("Elite Enemy");
    assert(created != 0);
    assert(session.entities.has_component<SpriteComponent>(created));
    assert(session.entities.has_component<ColliderComponent>(created));
    const TransformComponent transform = session.entities.get_component<TransformComponent>(created);
    assert(transform.position == glm::vec3(2.0f, 3.0f, 4.0f));
    assert(transform.scale == glm::vec2(2.0f, 3.0f));
    assert(transform.rotation == 45.0f);

    session.history.undo();
    assert(!session.entities.is_alive(created));
    assert(session.history.get_current_revision() == 0);

    session.history.redo();
    assert(session.entities.is_alive(created));
    assert(session.entities.find_by_tag("Elite Enemy") == created);
    assert(session.entities.has_component<SpriteComponent>(created));
    assert(session.entities.has_component<ColliderComponent>(created));
}

void test_component_data_can_be_configured_and_replayed() {
    TestSession session;
    EditorAiAuthoringRegistry registry;
    std::vector<EditorAiAuthoringAction> actions;
    queue(registry, actions, "propose_create_entity", {
        {"alias", "visual"}, {"name", "Visual"}, {"components", {"sprite"}}
    });
    queue(registry, actions, "propose_configure_component", {
        {"entity_alias", "visual"},
        {"component_key", "sprite"},
        {"patch_json", R"({"size":[96.0,48.0],"color":[255,64,32,255],"order_in_layer":7})"}
    });

    std::string error;
    auto group = registry.build_command_group(actions, session.context(), error);
    assert(group && error.empty());
    assert(session.history.execute_command(std::move(group)));
    const EntityID entity = session.entities.find_by_tag("Visual");
    assert(entity != 0);
    const SpriteComponent sprite = session.entities.get_component<SpriteComponent>(entity);
    assert(sprite.size == glm::vec2(96.0f, 48.0f));
    assert(sprite.color.r == 255 && sprite.color.g == 64 && sprite.color.b == 32 && sprite.color.a == 255);
    assert(sprite.order_in_layer == 7);

    session.history.undo();
    assert(!session.entities.is_alive(entity));
    session.history.redo();
    assert(session.entities.is_alive(entity));
    const SpriteComponent replayed = session.entities.get_component<SpriteComponent>(entity);
    assert(replayed.size == glm::vec2(96.0f, 48.0f));
    assert(replayed.order_in_layer == 7);
}

void test_2d_positions_default_or_preserve_depth() {
    TestSession session;
    EditorAiAuthoringRegistry registry;

    std::vector<EditorAiAuthoringAction> create_actions;
    queue(registry, create_actions, "propose_create_entity", {
        {"alias", "flat_enemy"}, {"name", "Flat Enemy"}, {"position", {12.0, 34.0}}
    });
    std::string error;
    auto create_group = registry.build_command_group(create_actions, session.context(), error);
    assert(create_group && error.empty());
    assert(session.history.execute_command(std::move(create_group)));
    const EntityID created = session.entities.find_by_tag("Flat Enemy");
    assert(created != 0);
    assert(session.entities.get_component<TransformComponent>(created).position == glm::vec3(12.0f, 34.0f, 0.0f));

    auto& transform = session.entities.get_component<TransformComponent>(created);
    transform.position.z = 9.0f;
    std::vector<EditorAiAuthoringAction> edit_actions;
    queue(registry, edit_actions, "propose_set_transform", {
        {"entity_id", created}, {"position", {7.0, 8.0}}
    });
    auto edit_group = registry.build_command_group(edit_actions, session.context(), error);
    assert(edit_group && error.empty());
    assert(session.history.execute_command(std::move(edit_group)));
    assert(session.entities.get_component<TransformComponent>(created).position == glm::vec3(7.0f, 8.0f, 9.0f));

    std::vector<EditorAiAuthoringAction> invalid_actions;
    assert(!registry.queue_action("propose_set_transform", {
        {"entity_id", created}, {"position", {1.0}}
    }, invalid_actions).success);
}

void test_ai_created_sprite_resolves_white_runtime_texture() {
    SDL_Surface* surface = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    assert(renderer);
    AssetManager assets;
    {
        TestSession session;
        EditorAiAuthoringRegistry registry;
        std::vector<EditorAiAuthoringAction> actions;
        queue(registry, actions, "propose_create_entity", {
            {"alias", "visible_sprite"},
            {"name", "Visible Sprite"},
            {"components", {"sprite"}}
        });

        EditorAiAuthoringContext context = session.context();
        context.serialization = {
            .renderer = renderer,
            .asset_manager = &assets
        };
        std::string error;
        auto group = registry.build_command_group(actions, context, error);
        assert(group && error.empty());
        assert(session.history.execute_command(std::move(group)));

        const EntityID created = session.entities.find_by_tag("Visible Sprite");
        assert(created != 0);
        const auto& sprite = session.entities.get_component<SpriteComponent>(created);
        assert(sprite.texture != nullptr);
        assert(sprite.texture->get_width() == 4);
        assert(sprite.texture->get_height() == 4);
        assert(sprite.color.a == 255);
    }
    assets.clear();
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

void test_created_alias_hierarchy_survives_undo_redo() {
    TestSession session;
    EditorAiAuthoringRegistry registry;
    std::vector<EditorAiAuthoringAction> actions;
    queue(registry, actions, "propose_create_entity", {{"alias", "parent"}, {"name", "Parent"}});
    queue(registry, actions, "propose_create_entity", {
        {"alias", "child"}, {"name", "Child"}, {"parent_entity_alias", "parent"}
    });

    std::string error;
    auto group = registry.build_command_group(actions, session.context(), error);
    assert(group && session.history.execute_command(std::move(group)));
    const EntityID parent = session.entities.find_by_tag("Parent");
    const EntityID child = session.entities.find_by_tag("Child");
    assert(parent != 0 && child != 0);
    assert(session.entities.get_component<HierarchyComponent>(child).parent == parent);

    session.history.undo();
    assert(!session.entities.is_alive(parent));
    assert(!session.entities.is_alive(child));
    session.history.redo();
    assert(session.entities.get_component<HierarchyComponent>(child).parent == parent);
}

void test_failed_action_rolls_back_previous_actions() {
    TestSession session;
    const EntityID entity = create_basic_entity(session, "Existing");
    session.entities.add_component<SpriteComponent>(entity, SpriteComponent{});
    const TransformComponent before = session.entities.get_component<TransformComponent>(entity);

    EditorAiAuthoringRegistry registry;
    std::vector<EditorAiAuthoringAction> actions;
    queue(registry, actions, "propose_set_transform", {
        {"entity_id", entity}, {"position", {99.0, 50.0, 0.0}}
    });
    queue(registry, actions, "propose_add_component", {
        {"entity_id", entity}, {"component_key", "sprite"}
    });

    std::string error;
    auto group = registry.build_command_group(actions, session.context(), error);
    assert(group);
    assert(!session.history.execute_command(std::move(group)));
    assert(session.entities.get_component<TransformComponent>(entity).position == before.position);
    assert(!session.history.can_undo());
}

void test_locked_missing_and_forward_alias_targets_are_rejected() {
    TestSession session;
    const EntityID locked = create_basic_entity(session, "Locked");
    session.state_store->set_locked(locked, true);
    EditorAiAuthoringRegistry registry;
    std::string error;

    std::vector<EditorAiAuthoringAction> locked_actions;
    queue(registry, locked_actions, "propose_delete_entity", {{"entity_id", locked}});
    assert(!registry.build_command_group(locked_actions, session.context(), error));
    assert(error.find("locked") != std::string::npos);

    error.clear();
    std::vector<EditorAiAuthoringAction> missing_actions;
    queue(registry, missing_actions, "propose_rename_entity", {
        {"entity_id", 999999}, {"name", "Missing"}
    });
    assert(!registry.build_command_group(missing_actions, session.context(), error));
    assert(error.find("missing") != std::string::npos);

    error.clear();
    std::vector<EditorAiAuthoringAction> forward_alias;
    const EditorAiToolResult invalid_alias = registry.queue_action("propose_set_transform", {
        {"entity_alias", "later"}, {"position", {1.0, 2.0, 3.0}}
    }, forward_alias);
    assert(!invalid_alias.success);
    assert(invalid_alias.error.find("find_scene_entities") != std::string::npos);
    assert(forward_alias.empty());
}

void test_existing_entity_delete_is_reversible() {
    TestSession session;
    const EntityID entity = create_basic_entity(session, "Delete Me");
    EditorAiAuthoringRegistry registry;
    std::vector<EditorAiAuthoringAction> actions;
    queue(registry, actions, "propose_delete_entity", {{"entity_id", entity}});

    std::string error;
    auto group = registry.build_command_group(actions, session.context(), error);
    assert(group && error.empty());
    assert(session.history.execute_command(std::move(group)));
    assert(!session.entities.is_alive(entity));
    assert(session.history.get_undo_count() == 1);

    session.history.undo();
    assert(session.entities.is_alive(entity));
    assert(session.entities.find_by_tag("Delete Me") == entity);
}

void test_bulk_duplicate_and_delete_are_single_reversible_actions() {
    TestSession session;
    const EntityID first = create_basic_entity(session, "First");
    const EntityID second = create_basic_entity(session, "Second");
    EditorAiAuthoringRegistry registry;

    std::vector<EditorAiAuthoringAction> duplicate;
    queue(registry, duplicate, "propose_duplicate_entities", {
        {"entity_ids", {first, second}}
    });
    std::string error;
    auto duplicate_group = registry.build_command_group(duplicate, session.context(), error);
    assert(duplicate_group && error.empty());
    assert(session.history.execute_command(std::move(duplicate_group)));
    assert(session.entities.get_living_entity_count() == 4);
    assert(session.history.get_undo_count() == 1);
    session.history.undo();
    assert(session.entities.get_living_entity_count() == 2);
    session.history.redo();
    assert(session.entities.get_living_entity_count() == 4);

    std::vector<EditorAiAuthoringAction> remove;
    queue(registry, remove, "propose_delete_entities", {
        {"entity_ids", {first, second}}
    });
    auto delete_group = registry.build_command_group(remove, session.context(), error);
    assert(delete_group && error.empty());
    assert(session.history.execute_command(std::move(delete_group)));
    assert(!session.entities.is_alive(first));
    assert(!session.entities.is_alive(second));
    session.history.undo();
    assert(session.entities.is_alive(first));
    assert(session.entities.is_alive(second));
}

void test_asset_handles_and_prefabs_are_validated_from_frozen_context() {
    EditorAiAuthoringRegistry registry;
    const nlohmann::json frozen_context{
        {"entity_index", nlohmann::json::array({{
            {"entity_id", 42},
            {"components", nlohmann::json::array({"sprite"})}
        }})},
        {"asset_index", nlohmann::json::array({
            {{"asset_handle", "0000000000000001"}, {"path", "assets/hero.png"},
             {"type", "texture"}, {"status", "ready"}},
            {{"asset_handle", "0000000000000002"}, {"path", "assets/enemy.prefab"},
             {"type", "prefab"}, {"status", "ready"}},
            {{"asset_handle", "0000000000000003"}, {"path", "assets/broken.png"},
             {"type", "texture"}, {"status", "missing_source"}}
        })}
    };

    std::vector<EditorAiAuthoringAction> actions;
    assert(registry.queue_action(
        "propose_configure_component",
        {{"entity_id", 42}, {"component_key", "sprite"},
         {"patch_json", R"({"texture_handle":"0000000000000001"})"}},
        actions,
        &frozen_context
    ).success);
    assert(!registry.queue_action(
        "propose_configure_component",
        {{"entity_id", 42}, {"component_key", "sprite"},
         {"patch_json", R"({"texture_handle":"0000000000000002"})"}},
        actions,
        &frozen_context
    ).success);
    assert(!registry.queue_action(
        "propose_configure_component",
        {{"entity_id", 42}, {"component_key", "sprite"},
         {"patch_json", R"({"texture_handle":"0000000000000003"})"}},
        actions,
        &frozen_context
    ).success);

    std::vector<EditorAiAuthoringAction> prefab_actions;
    const EditorAiToolResult prefab = registry.queue_action(
        "propose_instantiate_prefab",
        {{"asset_handle", "0000000000000002"}, {"alias", "enemy_prefab"}},
        prefab_actions,
        &frozen_context
    );
    assert(prefab.success);
    assert(prefab_actions.size() == 1);
    assert(!registry.queue_action(
        "propose_instantiate_prefab",
        {{"asset_handle", "0000000000000001"}, {"alias", "not_a_prefab"}},
        prefab_actions,
        &frozen_context
    ).success);
}

void test_generated_png_import_is_allowlisted_and_reversible() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_ai_generated_texture_tests";
    std::error_code filesystem_error;
    fs::remove_all(root, filesystem_error);
    const fs::path preview = root / "Library/EditorAi/TexturePreviews/texture_1_0.png";
    const std::vector<std::uint8_t> png_bytes{137, 80, 78, 71, 13, 10, 26, 10, 1, 2, 3, 4};
    assert(FileSystem::write_bytes(preview.string(), png_bytes));

    AssetDatabase database(root);
    TestSession session;
    const EntityID target = create_basic_entity(session, "Generated Texture Target");
    session.entities.add_component<SpriteComponent>(target, SpriteComponent{});
    const EntityID second_target = create_basic_entity(session, "Second Generated Texture Target");
    session.entities.add_component<SpriteComponent>(second_target, SpriteComponent{});
    EditorAiAuthoringContext context = session.context();
    context.serialization.asset_database = &database;
    EditorAiAuthoringRegistry registry;
    const std::string generated_source = "/codex/generated/image.png";
    const nlohmann::json frozen_context{
        {"entity_index", nlohmann::json::array({{
            {"entity_id", target},
            {"components", nlohmann::json::array({"tag", "transform", "sprite"})}
        }, {
            {"entity_id", second_target},
            {"components", nlohmann::json::array({"tag", "transform", "sprite"})}
        }})},
        {"generated_texture_previews", {
            {"image-1", {
                {"source_path", generated_source},
                {"preview_path", preview.string()}
            }}
        }}
    };

    std::vector<EditorAiAuthoringAction> rejected;
    assert(!registry.queue_action("propose_import_generated_texture", {
        {"generated_path", "/untrusted/image.png"},
        {"destination_path", "assets/generated/untrusted.png"},
        {"assignments", nlohmann::json::array()}
    }, rejected, &frozen_context).success);

    std::vector<EditorAiAuthoringAction> actions;
    assert(registry.queue_action("propose_import_generated_texture", {
        {"generated_path", generated_source},
        {"destination_path", "assets/generated/enemy.png"},
        {"assignments", nlohmann::json::array({
            {{"entity_id", target}, {"entity_alias", nullptr}, {"component_key", "sprite"}},
            {{"entity_id", second_target}, {"entity_alias", nullptr}, {"component_key", "sprite"}}
        })}
    }, actions, &frozen_context).success);
    assert(actions.at(0).arguments.at("generated_path") == preview.string());
    assert(!registry.queue_action("propose_import_generated_texture", {
        {"generated_path", generated_source},
        {"destination_path", "assets/generated/enemy.png"},
        {"assignments", nlohmann::json::array()}
    }, actions, &frozen_context).success);
    assert(!registry.queue_action("propose_import_generated_texture", {
        {"generated_path", generated_source},
        {"destination_path", "assets/generated/enemy-copy.png"},
        {"assignments", nlohmann::json::array()}
    }, actions, &frozen_context).success);
    assert(actions.size() == 1);

    std::string error;
    auto group = registry.build_command_group(actions, context, error);
    assert(group && error.empty());
    assert(session.history.execute_command(std::move(group)));
    const fs::path destination = root / "assets/generated/enemy.png";
    assert(FileSystem::read_bytes(destination.string()) == png_bytes);
    assert(database.find_by_path(destination) != nullptr);
    assert(database.find_by_path(destination)->type == AssetType::Texture);
    const AssetHandle handle = database.find_by_path(destination)->handle;
    assert(session.entities.get_component<SpriteComponent>(target).texture_handle == handle);
    assert(session.entities.get_component<SpriteComponent>(second_target).texture_handle == handle);
    session.history.undo();
    assert(!fs::exists(destination));
    assert(database.find_by_path(destination) != nullptr);
    assert(database.find_by_path(destination)->status == AssetStatus::MissingSource);
    assert(!session.entities.get_component<SpriteComponent>(target).texture_handle.is_valid());
    assert(!session.entities.get_component<SpriteComponent>(second_target).texture_handle.is_valid());
    session.history.redo();
    assert(FileSystem::read_bytes(destination.string()) == png_bytes);
    assert(database.find_by_path(destination)->handle == handle);
    assert(session.entities.get_component<SpriteComponent>(target).texture_handle == handle);
    assert(session.entities.get_component<SpriteComponent>(second_target).texture_handle == handle);

    const EntityID invalid_target = create_basic_entity(session, "Missing Sprite Target");
    std::vector<EditorAiAuthoringAction> invalid_actions{{
        "propose_import_generated_texture",
        {
            {"generated_path", preview.string()},
            {"destination_path", "assets/generated/rolled-back.png"},
            {"assignments", nlohmann::json::array({{
                {"entity_id", invalid_target},
                {"component_key", "sprite"}
            }})}
        },
        "Import then fail assignment"
    }};
    auto invalid_group = registry.build_command_group(invalid_actions, context, error);
    assert(invalid_group && error.empty());
    assert(!session.history.execute_command(std::move(invalid_group)));
    const fs::path rolled_back = root / "assets/generated/rolled-back.png";
    assert(!fs::exists(rolled_back));
    assert(database.find_by_path(rolled_back) == nullptr);
    fs::remove_all(root, filesystem_error);
}

void test_proposal_rejects_a_stale_scene_revision() {
    TestSession session;
    const EntityID entity = create_basic_entity(session, "Original");
    EditorAiAuthoringRegistry registry;

    std::vector<EditorAiAuthoringAction> first_actions;
    queue(registry, first_actions, "propose_set_transform", {
        {"entity_id", entity}, {"position", {4.0, 5.0, 0.0}}
    });
    std::string description;
    std::string error;
    assert(registry.execute_proposal(
        first_actions, session.context(), session.history, 0, description, error
    ));
    assert(session.history.get_current_revision() == 1);

    std::vector<EditorAiAuthoringAction> stale_actions;
    queue(registry, stale_actions, "propose_rename_entity", {
        {"entity_id", entity}, {"name", "Stale Rename"}
    });
    assert(!registry.execute_proposal(
        stale_actions, session.context(), session.history, 0, description, error
    ));
    assert(error.find("edited after") != std::string::npos);
    assert(session.entities.get_component<TagComponent>(entity).tag == "Original");
    assert(session.history.get_undo_count() == 1);
}

} // namespace

int main() {
    test_tool_validation_and_limits();
    test_alias_session_executes_as_one_undoable_transaction();
    test_component_data_can_be_configured_and_replayed();
    test_2d_positions_default_or_preserve_depth();
    test_ai_created_sprite_resolves_white_runtime_texture();
    test_created_alias_hierarchy_survives_undo_redo();
    test_failed_action_rolls_back_previous_actions();
    test_locked_missing_and_forward_alias_targets_are_rejected();
    test_existing_entity_delete_is_reversible();
    test_bulk_duplicate_and_delete_are_single_reversible_actions();
    test_asset_handles_and_prefabs_are_validated_from_frozen_context();
    test_generated_png_import_is_allowlisted_and_reversible();
    test_proposal_rejects_a_stale_scene_revision();
    std::cout << "Editor AI authoring tests passed\n";
    return 0;
}
