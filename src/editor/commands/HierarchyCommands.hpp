#pragma once

#include "IEditorCommand.hpp"
#include "EntityCommands.hpp"
#include "../../ecs/EntityManager.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../EditorSelection.hpp"
#include <memory>
#include <string>
#include <vector>

#include <optional>

namespace Engine {

class EditorEntityStateStore;

class RemoveMissingHierarchyChildReferenceCommand final : public IEditorCommand {
public:
    RemoveMissingHierarchyChildReferenceCommand(EntityManager* em, EntityID parent, EntityID missing_child);

    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }
    [[nodiscard]] bool is_valid() const { return m_valid; }

private:
    EntityManager* m_em{nullptr};
    EntityID m_parent{0};
    std::vector<EntityID> m_before;
    std::vector<EntityID> m_after;
    bool m_valid{false};
    std::string m_description{"Remove Missing Hierarchy Child"};
};

class ReparentEntityCommand final : public IEditorCommand {
public:
    ReparentEntityCommand(
        EntityManager* em,
        EntityID entity,
        EntityID new_parent,
        std::shared_ptr<EditorSelection> selection = nullptr,
        std::string custom_description = "",
        const EditorEntityStateStore* state_store = nullptr,
        std::optional<size_t> target_index = std::nullopt
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
    size_t m_old_index{0};
    std::optional<size_t> m_target_index{std::nullopt};

    bool m_had_transform{false};
    TransformComponent m_old_transform;
    TransformComponent m_new_transform;

    bool m_valid{false};
    std::string m_description;
};

struct ReparentBatchEntry {
    EntityID entity{0};
    EntityID old_parent{0};
    size_t old_index{0};
    std::optional<size_t> target_index{std::nullopt};
    TransformComponent old_transform;
    TransformComponent new_transform;
};

class ReparentEntitiesCommand final : public IEditorCommand {
public:
    ReparentEntitiesCommand(
        EntityManager* em,
        std::vector<EntityID> entities,
        EntityID new_parent,
        std::shared_ptr<EditorSelection> selection = nullptr,
        std::string custom_description = "",
        const EditorEntityStateStore* state_store = nullptr,
        std::optional<size_t> target_index = std::nullopt
    );

    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }
    [[nodiscard]] bool is_valid() const { return m_valid; }

private:
    EntityManager* m_em{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    std::vector<EntityID> m_original_selection;
    EntityID m_new_parent{0};
    std::optional<size_t> m_target_index{std::nullopt};
    std::vector<ReparentBatchEntry> m_entries;
    bool m_valid{false};
    std::string m_description{"Reparent Entities"};
};

class ReorderEntityCommand final : public IEditorCommand {
public:
    ReorderEntityCommand(
        EntityManager* em,
        EntityID entity,
        size_t new_index,
        std::shared_ptr<EditorSelection> selection = nullptr,
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
    EntityID m_parent{0};
    size_t m_old_index{0};
    size_t m_new_index{0};
    bool m_valid{false};
    std::string m_description{"Reorder Entity"};
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
