#pragma once

#include "IEditorCommand.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Engine {

// Executes several editor commands as one history entry. Failed execution and
// failed redo roll back every child already applied during that attempt.
class EditorCommandGroup final : public IEditorCommand {
public:
    explicit EditorCommandGroup(std::string description);

    void add_command(std::unique_ptr<IEditorCommand> command);
    [[nodiscard]] bool empty() const { return m_commands.empty(); }
    [[nodiscard]] std::size_t size() const { return m_commands.size(); }

    bool execute() override;
    bool undo() override;
    bool redo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }

private:
    enum class State {
        Fresh,
        Applied,
        Undone
    };

    std::vector<std::unique_ptr<IEditorCommand>> m_commands;
    std::string m_description;
    State m_state{State::Fresh};
};

} // namespace Engine
