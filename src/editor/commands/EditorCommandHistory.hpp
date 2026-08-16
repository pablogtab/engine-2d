#pragma once

#include "IEditorCommand.hpp"
#include <vector>
#include <memory>
#include <string>

namespace Engine {

class EditorCommandHistory {
public:
    explicit EditorCommandHistory(size_t max_history = 100)
        : m_max_history(max_history) {}

    // Executes a new command and pushes it to the undo stack (clears redo stack)
    void execute_command(std::unique_ptr<IEditorCommand> command);

    // Pushes a command that was already executed interactively (e.g. Gizmo drag release)
    void push_executed_command(std::unique_ptr<IEditorCommand> command);

    // Reverses the top command
    void undo();

    // Re-applies the top undone command
    void redo();

    [[nodiscard]] bool can_undo() const { return !m_undo_stack.empty(); }
    [[nodiscard]] bool can_redo() const { return !m_redo_stack.empty(); }

    [[nodiscard]] std::string get_undo_description() const;
    [[nodiscard]] std::string get_redo_description() const;

    void clear();

    [[nodiscard]] size_t get_undo_count() const { return m_undo_stack.size(); }
    [[nodiscard]] size_t get_redo_count() const { return m_redo_stack.size(); }

private:
    size_t m_max_history{100};
    std::vector<std::unique_ptr<IEditorCommand>> m_undo_stack;
    std::vector<std::unique_ptr<IEditorCommand>> m_redo_stack;
};

} // namespace Engine
