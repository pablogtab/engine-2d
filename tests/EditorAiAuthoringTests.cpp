#include "editor/ai/EditorAiAuthoring.hpp"
#include "core/AssetManager.hpp"
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
    queue(registry, forward_alias, "propose_set_transform", {
        {"entity_alias", "later"}, {"position", {1.0, 2.0, 3.0}}
    });
    queue(registry, forward_alias, "propose_create_entity", {{"alias", "later"}, {"name", "Later"}});
    assert(!registry.build_command_group(forward_alias, session.context(), error));
    assert(error.find("before it is created") != std::string::npos);
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
    test_2d_positions_default_or_preserve_depth();
    test_ai_created_sprite_resolves_white_runtime_texture();
    test_created_alias_hierarchy_survives_undo_redo();
    test_failed_action_rolls_back_previous_actions();
    test_locked_missing_and_forward_alias_targets_are_rejected();
    test_proposal_rejects_a_stale_scene_revision();
    std::cout << "Editor AI authoring tests passed\n";
    return 0;
}
