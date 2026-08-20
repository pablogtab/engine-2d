#pragma once

#include "IEditorCommand.hpp"
#include "../EditorSelection.hpp"
#include "EntityCommands.hpp"
#include "../../scene/PrefabSerializer.hpp"
#include "../../scene/EntitySubtreeArchive.hpp"
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_set>

struct SDL_Renderer;

namespace Engine {

class EntityManager;
class EditorEntityStateStore;

class InstantiatePrefabCommand : public IEditorCommand {
public:
    InstantiatePrefabCommand(
        EntityManager* em,
        std::shared_ptr<EditorSelection> selection,
        std::string prefab_filepath_or_json,
        bool is_raw_json,
        std::optional<TransformComponent> desired_world_transform,
        EntityID target_parent = 0,
        SDL_Renderer* renderer = nullptr,
        const EditorEntityStateStore* state_store = nullptr
    );

    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }
    [[nodiscard]] EntityID get_root_entity() const { return m_root_entity; }
    [[nodiscard]] const std::vector<EntityID>& get_created_entities() const { return m_created_entity_ids; }
    [[nodiscard]] bool is_valid() const { return m_valid; }

private:
    EntityManager* m_em{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    std::string m_json_content;
    std::optional<TransformComponent> m_desired_world_transform;
    EntityID m_target_parent{0};
    SDL_Renderer* m_renderer{nullptr};
    std::string m_description{"Instantiate Prefab"};
    bool m_valid{true};

    // Stored after initial execute for self-contained Undo/Redo without disk dependency
    std::string m_prefab_filepath;
    bool m_is_raw_json{false};
    EntityID m_root_entity{0};
    std::vector<EntityID> m_created_entity_ids;
    std::vector<EntitySnapshot> m_snapshots;
    std::unordered_set<EntityID> m_previous_selection;
};

class UnpackPrefabCommand : public IEditorCommand {
public:
    UnpackPrefabCommand(
        EntityManager* em,
        std::shared_ptr<EditorSelection> selection,
        EntityID root_entity,
        const EditorEntityStateStore* state_store = nullptr
    );

    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }
    [[nodiscard]] bool is_valid() const { return m_valid; }

private:
    EntityManager* m_em{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    EntityID m_root_entity{0};
    PrefabLinkComponent m_stored_link;
    std::string m_description{"Unpack Prefab"};
    bool m_valid{true};
};

class RevertPrefabCommand : public IEditorCommand {
public:
    RevertPrefabCommand(
        EntityManager* em,
        std::shared_ptr<EditorSelection> selection,
        EntityID root_entity,
        SDL_Renderer* renderer = nullptr,
        const EditorEntityStateStore* state_store = nullptr
    );

    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }
    [[nodiscard]] bool is_valid() const { return m_valid; }
    [[nodiscard]] EntityID get_new_root_entity() const { return m_new_root_entity; }

private:
    EntityManager* m_em{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    EntityID m_original_root_entity{0};
    std::string m_prefab_filepath;
    std::string m_json_content;
    SDL_Renderer* m_renderer{nullptr};
    std::string m_description{"Revert Prefab Contents"};
    bool m_valid{true};

    // Stored after initial execute for self-contained Undo/Redo without disk dependency
    EntityID m_new_root_entity{0};
    std::vector<EntityID> m_before_entity_ids;
    std::vector<EntitySnapshot> m_before_snapshots;
    std::vector<EntityID> m_after_entity_ids;
    std::vector<EntitySnapshot> m_after_snapshots;
    std::unordered_set<EntityID> m_previous_selection;
};

class ApplyPrefabCommand : public IEditorCommand {
public:
    ApplyPrefabCommand(
        EntityManager* em,
        EntityID root_entity,
        SDL_Renderer* renderer = nullptr,
        const EditorEntityStateStore* state_store = nullptr
    );

    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }
    [[nodiscard]] bool is_valid() const { return m_valid; }
    [[nodiscard]] const std::string& get_prefab_filepath() const { return m_prefab_filepath; }

private:
    EntityManager* m_em{nullptr};
    EntityID m_root_entity{0};
    std::string m_prefab_filepath;
    std::string m_previous_file_content;
    std::string m_new_file_content;
    SDL_Renderer* m_renderer{nullptr};
    std::string m_description{"Apply to Prefab Asset"};
    bool m_valid{true};
    bool m_first_execute{true};
};

} // namespace Engine
