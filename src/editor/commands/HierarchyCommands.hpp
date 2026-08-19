#pragma once

#include "IEditorCommand.hpp"
#include "../../ecs/EntityManager.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../EditorSelection.hpp"
#include <memory>
#include <string>

namespace Engine {

class ReparentEntityCommand final : public IEditorCommand {
public:
    ReparentEntityCommand(
        EntityManager* em,
        EntityID entity,
        EntityID new_parent,
        std::shared_ptr<EditorSelection> selection = nullptr,
        std::string custom_description = ""
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

} // namespace Engine
