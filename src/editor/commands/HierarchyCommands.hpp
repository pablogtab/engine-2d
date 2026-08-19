#pragma once

#include "IEditorCommand.hpp"
#include "EntityCommands.hpp"
#include "../../ecs/EntityManager.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../EditorSelection.hpp"
#include <memory>
#include <string>
#include <vector>

namespace Engine {

class EditorEntityStateStore;

class ReparentEntityCommand final : public IEditorCommand {
public:
    ReparentEntityCommand(
        EntityManager* em,
        EntityID entity,
        EntityID new_parent,
        std::shared_ptr<EditorSelection> selection = nullptr,
        std::string custom_description = "",
        const EditorEntityStateStore* state_store = nullptr
    );

    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }
    [[nodiscard]] bool is_valid() const { return m_valid; }

private:
    EntityManager* m_em{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    EntityID m_entity{0};
    EntityID m_old_parent{0};
    EntityID m_new_parent{0};

    bool m_had_transform{false};
    TransformComponent m_old_transform;
    TransformComponent m_new_transform;

    bool m_valid{false};
    std::string m_description;
};

struct GroupChildEntry {
    EntityID entity{0};
    EntityID old_parent{0};
    TransformComponent old_local_transform;
    TransformComponent new_local_transform;
};

class GroupSelectedEntitiesCommand final : public IEditorCommand {
public:
    GroupSelectedEntitiesCommand(
        EntityManager* em,
        std::shared_ptr<EditorSelection> selection,
        const std::vector<EntityID>& selected_entities,
        std::string group_name = "Group",
        const EditorEntityStateStore* state_store = nullptr
    );

    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }
    [[nodiscard]] bool is_valid() const { return m_valid; }
    [[nodiscard]] EntityID get_group_entity() const { return m_group_id; }

private:
    EntityManager* m_em{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    std::vector<EntityID> m_original_selection;
    std::string m_group_name{"Group"};

    EntityID m_group_id{0};
    EntityID m_group_parent{0};
    TransformComponent m_group_transform;

    std::vector<GroupChildEntry> m_children;
    bool m_valid{false};
    std::string m_description{"Group Selected Entities"};
};

struct UngroupChildEntry {
    EntityID entity{0};
    TransformComponent old_local_transform;
    TransformComponent new_local_transform;
};

struct UngroupEntry {
    EntityID group_id{0};
    EntityID group_parent{0};
    EntitySnapshot group_snapshot;
    std::vector<UngroupChildEntry> children;
};

class UngroupEntitiesCommand final : public IEditorCommand {
public:
    UngroupEntitiesCommand(
        EntityManager* em,
        std::shared_ptr<EditorSelection> selection,
        const std::vector<EntityID>& target_entities,
        const EditorEntityStateStore* state_store = nullptr
    );

    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }
    [[nodiscard]] bool is_valid() const { return m_valid; }

private:
    EntityManager* m_em{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    std::vector<EntityID> m_original_selection;
    std::vector<UngroupEntry> m_groups;
    std::vector<EntityID> m_unpacked_children;
    bool m_valid{false};
    std::string m_description{"Ungroup Entities"};
};

} // namespace Engine
