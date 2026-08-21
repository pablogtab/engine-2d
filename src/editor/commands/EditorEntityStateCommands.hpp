#pragma once

#include "IEditorCommand.hpp"
#include "../state/EditorEntityStateStore.hpp"
#include "../../ecs/EntityManager.hpp"
#include <vector>
#include <memory>
#include <string>

namespace Engine {

enum class EditorEntityStateField {
    Locked,
    VisibleInEditor
};

class SetEditorEntityStateCommand final : public IEditorCommand {
public:
    struct Entry {
        EntityID id{0};
        bool before_value{false};
        bool after_value{false};
    };

    SetEditorEntityStateCommand(
        const EntityManager* em,
        std::shared_ptr<EditorEntityStateStore> store,
        EditorEntityStateField field,
        std::vector<Entry> entries,
        std::string description = ""
    );

    // Convenience constructor for single entity
    SetEditorEntityStateCommand(
        const EntityManager* em,
        std::shared_ptr<EditorEntityStateStore> store,
        EditorEntityStateField field,
        EntityID entity,
        bool new_value,
        std::string description = ""
    );

    // Convenience constructor for multiple entities with same new value
    SetEditorEntityStateCommand(
        const EntityManager* em,
        std::shared_ptr<EditorEntityStateStore> store,
        EditorEntityStateField field,
        const std::vector<EntityID>& entities,
        bool new_value,
        std::string description = ""
    );

    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }
    [[nodiscard]] EditorCommandScope get_scope() const override { return EditorCommandScope::EditorSession; }
    [[nodiscard]] bool is_valid() const { return m_valid; }

private:
    const EntityManager* m_em{nullptr};
    std::shared_ptr<EditorEntityStateStore> m_store;
    EditorEntityStateField m_field{EditorEntityStateField::Locked};
    std::vector<Entry> m_entries;
    std::string m_description;
    bool m_valid{false};
};

class SetComponentOrderCommand final : public IEditorCommand {
public:
    SetComponentOrderCommand(
        const EntityManager* em,
        std::shared_ptr<EditorEntityStateStore> store,
        EntityID entity,
        std::vector<std::string> before,
        std::vector<std::string> after
    );

    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return "Reorder Components"; }
    [[nodiscard]] bool is_valid() const { return m_valid; }

private:
    bool apply(const std::vector<std::string>& order);

    const EntityManager* m_em{nullptr};
    std::shared_ptr<EditorEntityStateStore> m_store;
    EntityID m_entity{0};
    std::vector<std::string> m_before;
    std::vector<std::string> m_after;
    bool m_valid{false};
};

} // namespace Engine
